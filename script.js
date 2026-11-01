
// ============================================================================
// VARIÁVEIS GLOBAIS DO LABORATÓRIO ACÚSTICO — AUTOR: ROBSON ARAUJO
// ============================================================================
let audioCtx = null;
let currentSource = null;
let currentGainNode = null;

let rawSamplesGlobal = null;
let infraFilteredGlobal = null;
let normOrigGlobal = null;
let normInfraGlobal = null;
let normUltraGlobal = null;

let sampleRateGlobal = 49718;
let totalDurationSec = 0;

// Estado da Agulha de Reprodução (Seek / Playhead)
let selectedSeekSec = 0;       // Ponto selecionado pelo clique no gráfico ou barra (em segundos reais da gravação)
let currentPlayheadSec = 0;    // Posição instantânea da agulha durante a reprodução
let activePlayMode = 'orig';   // 'orig' (1x), 'infra' (25x), 'ultra' (÷6x)
let isPlayingAudio = false;
let playStartWallTime = 0;
let playStartOffsetSec = 0;
let currentPlaybackRate = 1.0;
let animFrameId = null;

// Cache de imagem rápida para desenhar a agulha a 60 FPS sem recalcular o Waterfall
let offscreenTimeCanvas = document.createElement('canvas');
let offscreenWaterfallCanvas = document.createElement('canvas');

// Exportação WAV
let lastAudifiedSamples = null;
let lastAudifiedRate = 44100;
let lastAudifiedName = "audificado.wav";
let manualGainMultiplier = 5.0; // 5.0x (+14 dB)

let fftPlotState = null;

// Eventos de Drag & Drop e Arquivo
const fileInput = document.getElementById('wavInput');
fileInput.addEventListener('change', e => {
  if (e.target.files.length) loadWavFile(e.target.files[0]);
});

window.addEventListener('dragover', e => {
  e.preventDefault();
  document.body.classList.add('drag-active');
});

window.addEventListener('dragleave', e => {
  if (e.target === document.body || e.clientY <= 0 || e.clientX <= 0) {
    document.body.classList.remove('drag-active');
  }
});

window.addEventListener('drop', e => {
  e.preventDefault();
  document.body.classList.remove('drag-active');
  if (e.dataTransfer.files.length) loadWavFile(e.dataTransfer.files[0]);
});

// Atalho de teclado: Barra de Espaço dá Play/Pause exatamente do trecho clicado
window.addEventListener('keydown', e => {
  if (e.code === 'Space' && rawSamplesGlobal && document.activeElement.tagName !== 'BUTTON') {
    e.preventDefault();
    togglePlayFromSelectedPoint();
  }
});

window.addEventListener('resize', () => {
  if (rawSamplesGlobal && infraFilteredGlobal) {
    buildTimeAndEnvelopeCache(rawSamplesGlobal, infraFilteredGlobal, sampleRateGlobal);
    buildFullWaterfallCache(rawSamplesGlobal, sampleRateGlobal);
    drawTimeAndWaterfallWithPlayhead(isPlayingAudio ? currentPlayheadSec : selectedSeekSec);
  } else {
    drawInitialEmptyScreens();
  }
  if (fftPlotState) {
    syncCanvasResolution(document.getElementById('cFft'));
    drawFftSpectrumWithPeakBadge(null);
  }
});

function syncCanvasResolution(canvas) {
  const rect = canvas.parentElement.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const targetW = Math.max(300, Math.floor(rect.width * dpr));
  const targetH = Math.max(120, Math.floor(rect.height * dpr));
  if (canvas.width !== targetW || canvas.height !== targetH) {
    canvas.width = targetW;
    canvas.height = targetH;
  }
}

function drawInitialEmptyScreens() {
  const ids = ['cFft', 'cTime', 'cWaterfall'];
  ids.forEach(id => {
    const cv = document.getElementById(id);
    syncCanvasResolution(cv);
    const ctx = cv.getContext('2d');
    const W = cv.width, H = cv.height;
    ctx.fillStyle = '#04070d';
    ctx.fillRect(0, 0, W, H);

    ctx.strokeStyle = '#111c2d';
    ctx.lineWidth = 1;
    for (let i = 1; i < 8; i++) {
      const x = (i / 8) * W;
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
    }
    for (let j = 1; j < 4; j++) {
      const y = (j / 4) * H;
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
    }

    ctx.fillStyle = '#475569';
    ctx.font = 'bold 13px Segoe UI, sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('Aguardando carregamento de arquivo EVENT_XXXX.WAV do Cartão MicroSD...', W / 2, H / 2);

    ctx.fillStyle = 'rgba(251, 191, 36, 0.45)';
    ctx.font = 'bold 11px Segoe UI, sans-serif';
    ctx.textAlign = 'right';
    ctx.fillText('Laboratório Acústico • Autor: Robson Araujo', W - 12, 18);
  });
}

// ============================================================================
// CONTROLE DE GANHO MANUAL EM TEMPO REAL (0.5x ATÉ 30x)
// ============================================================================
function updateManualGain(val) {
  manualGainMultiplier = parseFloat(val);
  const dbVal = 20 * Math.log10(manualGainMultiplier);
  const sign = dbVal >= 0 ? "+" : "";
  document.getElementById('gainLabel').textContent =
    `${manualGainMultiplier.toFixed(1)}x (${sign}${dbVal.toFixed(1)} dB)`;

  if (currentGainNode && audioCtx) {
    currentGainNode.gain.setTargetAtTime(manualGainMultiplier, audioCtx.currentTime, 0.02);
  }
}

function setGainPreset(val) {
  const slider = document.getElementById('gainSlider');
  slider.value = val;
  updateManualGain(val);
}

// ============================================================================
// LEITURA E PREPARAÇÃO DOS BUFFERS DE AUDIFICAÇÃO (.WAV)
// ============================================================================
function loadWavFile(file) {
  stopAudio(true);
  const reader = new FileReader();
  reader.onload = function(evt) {
    const buffer = evt.target.result;
    const view = new DataView(buffer);

    const sampleRate = view.getUint32(24, true);
    const numSamples = (buffer.byteLength - 44) / 2;
    const samples = new Float32Array(numSamples);

    let sum = 0;
    for (let i = 0; i < numSamples; i++) {
      const s = view.getInt16(44 + i * 2, true) / 32768.0;
      samples[i] = s;
      sum += s;
    }
    const mean = sum / numSamples;
    for (let i = 0; i < numSamples; i++) samples[i] -= mean;

    rawSamplesGlobal = samples;
    sampleRateGlobal = sampleRate;
    totalDurationSec = numSamples / sampleRate;
    selectedSeekSec = 0;
    currentPlayheadSec = 0;

    const dur = totalDurationSec.toFixed(2);
    const recDate = file.lastModified ? new Date(file.lastModified) : new Date();
    const dateStr = recDate.toLocaleDateString('pt-BR') + ' ' + recDate.toLocaleTimeString('pt-BR');

    document.getElementById('fileInfo').innerHTML =
      `<strong style="color:#38bdf8">${file.name}</strong> • ${dur}s • 📅 ${dateStr}`;
    document.getElementById('mRate').textContent =
      `${sampleRate.toLocaleString()} Hz (Nyq: ${(sampleRate/2000).toFixed(1)}k)`;

    // Habilita controles e barra de reprodução
    document.getElementById('btnPlayOrig').disabled = false;
    document.getElementById('btnPlayInfra').disabled = false;
    document.getElementById('btnPlayUltra').disabled = false;
    document.getElementById('btnStop').disabled = false;
    document.getElementById('btnPlayFromCursor').disabled = false;
    document.getElementById('btnResetCursor').disabled = false;
    document.getElementById('timelineSlider').disabled = false;
    document.getElementById('timelineSlider').value = 0;

    updateTimeCounterUI(0);
    document.getElementById('audioStatus').textContent =
      `Clique em qualquer ponto do Osciloscópio/Waterfall ou aperte ▶️ Tocar Trecho`;

    processSignalAndRenderAll(samples, sampleRate);
  };
  reader.readAsArrayBuffer(file);
}

// ============================================================================
// PROCESSAMENTO CIENTÍFICO (FFT 4096-PT + FILTROS + CACHE DE TELAS)
// ============================================================================
function processSignalAndRenderAll(samples, fs) {
  const N = samples.length;

  // 1. Filtro Passa-Baixas de 4ª Ordem (< 20 Hz) para Infrassom Puro
  const rcLow = 1.0 / (2.0 * Math.PI * 20.0);
  const dt = 1.0 / fs;
  const alphaLow = dt / (rcLow + dt);

  let stageBuf = new Float32Array(samples);
  for (let pass = 0; pass < 4; pass++) {
    let prev = stageBuf[0];
    for (let i = 1; i < N; i++) {
      prev = prev + alphaLow * (stageBuf[i] - prev);
      stageBuf[i] = prev;
    }
  }
  infraFilteredGlobal = stageBuf;

  // 2. Filtro Passa-Altas de 2ª Ordem (> 6 kHz) para Isolamento de Ultrassom
  const rcHigh = 1.0 / (2.0 * Math.PI * 6000.0);
  const alphaHigh = rcHigh / (rcHigh + dt);
  let hpStage1 = new Float32Array(N);
  hpStage1[0] = samples[0];
  for (let i = 1; i < N; i++) {
    hpStage1[i] = alphaHigh * (hpStage1[i - 1] + samples[i] - samples[i - 1]);
  }
  let hpFiltered = new Float32Array(N);
  hpFiltered[0] = hpStage1[0];
  for (let i = 1; i < N; i++) {
    hpFiltered[i] = alphaHigh * (hpFiltered[i - 1] + hpStage1[i] - hpStage1[i - 1]);
  }

  // Pré-normaliza os 3 buffers para resposta imediata ao clicar no gráfico
  normOrigGlobal = normalizeSignal(samples);
  normInfraGlobal = normalizeSignal(infraFilteredGlobal);
  normUltraGlobal = normalizeSignal(hpFiltered);

  // Constrói os gráficos base (Tela 2 e Tela 3) e desenha a agulha na posição 0.00s
  buildTimeAndEnvelopeCache(samples, infraFilteredGlobal, fs);
  buildFullWaterfallCache(samples, fs);
  drawTimeAndWaterfallWithPlayhead(selectedSeekSec);

  // 3. Espectro FFT de Alta Resolução (4096-pt)
  const fftSize = 4096;
  const halfFft = fftSize / 2;
  const magAvg = new Float32Array(halfFft);
  const magPeak = new Float32Array(halfFft);
  const magTonePeak = new Float32Array(halfFft);

  const step = fftSize / 2;
  let windowsCount = 0;

  for (let start = 0; start + fftSize <= N; start += step) {
    const re = new Float32Array(fftSize);
    const im = new Float32Array(fftSize);

    let sumY = 0, sumXY = 0;
    for (let i = 0; i < fftSize; i++) {
      const y = samples[start + i];
      sumY += y;
      sumXY += i * y;
    }
    const meanX = (fftSize - 1) * 0.5;
    const meanY = sumY / fftSize;
    const covXY = sumXY - fftSize * meanX * meanY;
    const varX = (fftSize * (fftSize * fftSize - 1)) / 12.0;
    const slope = covXY / varX;
    const intercept = meanY - slope * meanX;

    for (let i = 0; i < fftSize; i++) {
      const w = 0.5 * (1 - Math.cos(2 * Math.PI * i / (fftSize - 1)));
      re[i] = (samples[start + i] - (slope * i + intercept)) * w;
    }
    fftRadix2(re, im);

    for (let k = 1; k < halfFft; k++) {
      const m = Math.sqrt(re[k] * re[k] + im[k] * im[k]) / (fftSize * 0.25);
      magAvg[k] += m;
      if (m > magPeak[k]) magPeak[k] = m;
      if (k >= 4 && m > magTonePeak[k]) magTonePeak[k] = m;
    }
    windowsCount++;
  }

  const magCombined = new Float32Array(halfFft);
  const dbSpectrum = new Float32Array(halfFft);
  dbSpectrum[0] = -100;

  if (windowsCount > 0) {
    for (let k = 1; k < halfFft; k++) {
      magAvg[k] /= windowsCount;
      magCombined[k] = k < 4 ? magAvg[k] : (0.30 * magAvg[k] + 0.70 * magTonePeak[k]);
      dbSpectrum[k] = Math.max(-100, 20 * Math.log10(magCombined[k] + 1e-9));
    }
  }

  let totalEnergy = 0, infraEnergy = 0, ultraEnergy = 0;
  let maxMag = 0, peakBin = 1;
  let bestProminenceScore = -999;
  let prominentPeakBin = 1;

  for (let k = 1; k < halfFft; k++) {
    const freq = (k * fs) / fftSize;
    const e = magAvg[k] * magAvg[k];
    totalEnergy += e;
    if (freq <= 25.0) infraEnergy += e;
    if (freq >= 11500.0) ultraEnergy += e;

    if (k >= 5) {
      let localNeighbors = [];
      for (let d = -24; d <= 24; d++) {
        if (Math.abs(d) <= 3) continue;
        const idx = k + d;
        if (idx >= 5 && idx < halfFft) localNeighbors.push(dbSpectrum[idx]);
      }
      localNeighbors.sort((a, b) => a - b);
      const localFloorDb = localNeighbors.length
        ? localNeighbors[Math.floor(localNeighbors.length * 0.5)]
        : -95;

      const snrAboveFloor = dbSpectrum[k] - localFloorDb;
      const prominenceScore = snrAboveFloor * 1.6 + dbSpectrum[k] * 0.4;

      if (snrAboveFloor > 6.0 && prominenceScore > bestProminenceScore) {
        bestProminenceScore = prominenceScore;
        prominentPeakBin = k;
      }
    }

    if (magCombined[k] > maxMag) {
      maxMag = magCombined[k];
      peakBin = k;
    }
  }

  if (bestProminenceScore > -900) {
    peakBin = prominentPeakBin;
    maxMag = magCombined[peakBin];
  }

  let exactPeakBin = peakBin;
  if (peakBin > 1 && peakBin < halfFft - 1) {
    const y1 = Math.log(magCombined[peakBin - 1] + 1e-12);
    const y2 = Math.log(magCombined[peakBin] + 1e-12);
    const y3 = Math.log(magCombined[peakBin + 1] + 1e-12);
    const denom = 2 * (2 * y2 - y1 - y3);
    if (Math.abs(denom) > 1e-9) {
      const delta = (y1 - y3) / denom;
      if (Math.abs(delta) <= 0.6) exactPeakBin = peakBin + delta;
    }
  }

  const peakFreq = (exactPeakBin * fs) / fftSize;
  const peakDb = 20 * Math.log10(maxMag + 1e-9);
  const infraRatio = totalEnergy > 0 ? (infraEnergy / totalEnergy) * 100 : 0;
  const ultraRatio = totalEnergy > 0 ? (ultraEnergy / totalEnergy) * 100 : 0;

  document.getElementById('mPeak').textContent = `${peakFreq.toFixed(1)} Hz`;
  document.getElementById('mInfra').textContent = `${infraRatio.toFixed(1)} %`;
  document.getElementById('mUltra').textContent = `${ultraRatio.toFixed(1)} %`;

  const classEl = document.getElementById('mClass');
  if (peakFreq >= 11500) {
    classEl.textContent = `🦇 ULTRASSOM (${(peakFreq/1000).toFixed(2)} kHz)`;
    classEl.style.color = "#c084fc";
  } else if (peakFreq >= 20.0) {
    classEl.textContent = `🎵 SOM CAPTADO (${peakFreq.toFixed(1)} Hz)`;
    classEl.style.color = "#4ade80";
  } else if (infraRatio > 35 && peakFreq < 20.0) {
    classEl.textContent = `⚠️ INFRASSOM (${peakFreq.toFixed(1)} Hz)`;
    classEl.style.color = "#f87171";
  } else if (ultraRatio > 35) {
    classEl.textContent = "🦇 ULTRASSOM (>11.5 kHz)";
    classEl.style.color = "#c084fc";
  } else {
    classEl.textContent = `🎵 SOM CAPTADO (${peakFreq.toFixed(1)} Hz)`;
    classEl.style.color = "#4ade80";
  }

  const secondaryPeaks = [];
  const minBinSeparation = Math.max(8, Math.round((400 / (fs / 2)) * halfFft));
  for (let k = 3; k < halfFft - 3; k++) {
    if (Math.abs(k - peakBin) < minBinSeparation) continue;
    if (
      dbSpectrum[k] > dbSpectrum[k - 1] &&
      dbSpectrum[k] > dbSpectrum[k + 1] &&
      dbSpectrum[k] > dbSpectrum[k - 2] &&
      dbSpectrum[k] > dbSpectrum[k + 2] &&
      dbSpectrum[k] > peakDb - 28 &&
      dbSpectrum[k] > -68
    ) {
      secondaryPeaks.push({ bin: k, freq: (k * fs) / fftSize, db: dbSpectrum[k] });
    }
  }
  secondaryPeaks.sort((a, b) => b.db - a.db);
  const topSecondary = [];
  for (const cand of secondaryPeaks) {
    if (topSecondary.every(p => Math.abs(p.bin - cand.bin) >= minBinSeparation)) {
      topSecondary.push(cand);
      if (topSecondary.length >= 4) break;
    }
  }

  fftPlotState = {
    dbSpectrum,
    halfFft,
    fs,
    fftSize,
    peakBin: exactPeakBin,
    peakFreq,
    peakDb,
    secondaryPeaks: topSecondary
  };

  syncCanvasResolution(document.getElementById('cFft'));
  drawFftSpectrumWithPeakBadge(null);
}

// ============================================================================
// CONSTRUÇÃO DO CACHE DE IMAGEM PARA TELA 2 (OSCILOSCÓPIO) E TELA 3 (WATERFALL)
// ============================================================================
function buildTimeAndEnvelopeCache(samples, infraFiltered, fs) {
  const cv = document.getElementById('cTime');
  syncCanvasResolution(cv);
  offscreenTimeCanvas.width = cv.width;
  offscreenTimeCanvas.height = cv.height;

  const ctx = offscreenTimeCanvas.getContext('2d');
  const W = cv.width, H = cv.height;
  const N = samples.length;

  ctx.fillStyle = '#04070d';
  ctx.fillRect(0, 0, W, H);

  ctx.strokeStyle = '#111c2d';
  ctx.lineWidth = 1;
  for (let i = 1; i < 8; i++) {
    const x = (i / 8) * W;
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
  }
  for (let j = 1; j < 4; j++) {
    const y = (j / 4) * H;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
  }

  ctx.strokeStyle = '#1e293b';
  ctx.beginPath(); ctx.moveTo(0, H / 2); ctx.lineTo(W, H / 2); ctx.stroke();

  let maxRaw = 0.001, maxInf = 0.0001;
  for (let i = 0; i < N; i++) {
    if (Math.abs(samples[i]) > maxRaw) maxRaw = Math.abs(samples[i]);
    if (Math.abs(infraFiltered[i]) > maxInf) maxInf = Math.abs(infraFiltered[i]);
  }

  ctx.strokeStyle = '#38bdf8';
  ctx.lineWidth = 1.1;
  ctx.beginPath();
  for (let x = 0; x < W; x++) {
    const idx = Math.floor((x / W) * N);
    const y = (H / 2) - (samples[idx] / maxRaw) * (H * 0.42);
    if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();

  ctx.strokeStyle = '#f87171';
  ctx.lineWidth = 2.0;
  ctx.beginPath();
  for (let x = 0; x < W; x++) {
    const idx = Math.floor((x / W) * N);
    const y = (H / 2) - (infraFiltered[idx] / maxInf) * (H * 0.36);
    if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();

  const totalSec = N / fs;
  ctx.fillStyle = '#94a3b8';
  ctx.font = '10.5px Consolas, monospace';
  ctx.textAlign = 'center';
  for (let i = 0; i <= 4; i++) {
    const t = (i / 4) * totalSec;
    const x = Math.min(W - 18, Math.max(18, (i / 4) * W));
    ctx.fillText(`${t.toFixed(1)}s`, x, H - 5);
  }
}

function buildFullWaterfallCache(samples, fs) {
  const cv = document.getElementById('cWaterfall');
  syncCanvasResolution(cv);
  offscreenWaterfallCanvas.width = cv.width;
  offscreenWaterfallCanvas.height = cv.height;

  const ctx = offscreenWaterfallCanvas.getContext('2d');
  const W = cv.width, H = cv.height;

  const winSize = 512;
  const halfWin = winSize / 2;
  const imgData = ctx.createImageData(W, H);
  const data = imgData.data;

  for (let x = 0; x < W; x++) {
    const startIdx = Math.floor((x / W) * Math.max(1, samples.length - winSize));
    const re = new Float32Array(winSize);
    const im = new Float32Array(winSize);

    let localMean = 0;
    for (let i = 0; i < winSize; i++) localMean += samples[startIdx + i];
    localMean /= winSize;

    for (let i = 0; i < winSize; i++) {
      const w = 0.5 * (1 - Math.cos(2 * Math.PI * i / (winSize - 1)));
      re[i] = (samples[startIdx + i] - localMean) * w;
    }
    fftRadix2(re, im);

    for (let y = 0; y < H; y++) {
      const bin = Math.min(halfWin - 1, Math.max(1, Math.floor(((H - 1 - y) / H) * halfWin)));
      const mag = Math.sqrt(re[bin] * re[bin] + im[bin] * im[bin]);
      const db = 20 * Math.log10(mag + 1e-7);
      const norm = Math.max(0, Math.min(1, (db + 45) / 55));

      const [r, g, b] = thermalColorMap(norm);
      const p = (y * W + x) * 4;
      data[p] = r;
      data[p + 1] = g;
      data[p + 2] = b;
      data[p + 3] = 255;
    }
  }
  ctx.putImageData(imgData, 0, 0);

  const nyquist = fs / 2;
  ctx.fillStyle = 'rgba(4, 7, 13, 0.72)';
  ctx.fillRect(0, 0, 52, H);

  ctx.fillStyle = '#e2e8f0';
  ctx.font = '10px Consolas, monospace';
  ctx.textAlign = 'left';
  const freqMarks = [20000, 15000, 11500, 5000, 1000];
  for (const f of freqMarks) {
    if (f < nyquist) {
      const y = H - (f / nyquist) * H;
      ctx.fillText(`${(f/1000).toFixed(1)}k`, 4, Math.min(H - 4, Math.max(10, y + 3)));
      if (f === 11500) {
        ctx.save();
        ctx.setLineDash([4, 4]);
        ctx.strokeStyle = 'rgba(192, 132, 252, 0.65)';
        ctx.beginPath(); ctx.moveTo(52, y); ctx.lineTo(W, y); ctx.stroke();
        ctx.restore();
      }
    }
  }
}

// ============================================================================
// DESENHO DA AGULHA DE REPRODUÇÃO (PLAYHEAD) EM TEMPO REAL NAS TELAS 2 E 3
// ============================================================================
function drawTimeAndWaterfallWithPlayhead(timeSec) {
  if (!rawSamplesGlobal || totalDurationSec <= 0) return;
  const ratio = Math.max(0, Math.min(1, timeSec / totalDurationSec));

  // 1. Atualiza Tela 2 (Osciloscópio)
  const cvTime = document.getElementById('cTime');
  const ctxT = cvTime.getContext('2d');
  ctxT.drawImage(offscreenTimeCanvas, 0, 0);
  drawPlayheadOverlay(ctxT, cvTime.width, cvTime.height, ratio, timeSec, true);

  // 2. Atualiza Tela 3 (Espectrograma Waterfall)
  const cvWf = document.getElementById('cWaterfall');
  const ctxW = cvWf.getContext('2d');
  ctxW.drawImage(offscreenWaterfallCanvas, 0, 0);
  drawPlayheadOverlay(ctxW, cvWf.width, cvWf.height, ratio, timeSec, false);
}

function drawPlayheadOverlay(ctx, W, H, ratio, timeSec, showSelectedMarker) {
  // Se estiver tocando, mostra também uma linha tracejada onde o usuário clicou originalmente
  if (showSelectedMarker && isPlayingAudio) {
    const selRatio = Math.max(0, Math.min(1, selectedSeekSec / totalDurationSec));
    const selX = selRatio * W;
    ctx.save();
    ctx.setLineDash([3, 3]);
    ctx.strokeStyle = 'rgba(56, 189, 248, 0.75)';
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.moveTo(selX, 0);
    ctx.lineTo(selX, H);
    ctx.stroke();
    ctx.restore();
  }

  const x = ratio * W;

  // Linha vertical dourada da agulha de reprodução
  ctx.save();
  ctx.strokeStyle = '#fbbf24';
  ctx.lineWidth = 2.0;
  ctx.shadowColor = 'rgba(251, 191, 36, 0.8)';
  ctx.shadowBlur = 6;
  ctx.beginPath();
  ctx.moveTo(x, 0);
  ctx.lineTo(x, H);
  ctx.stroke();
  ctx.restore();

  // Etiqueta no topo da agulha
  const label = `▼ ${timeSec.toFixed(2)}s`;
  ctx.font = 'bold 10.5px Consolas, monospace';
  const tw = ctx.measureText(label).width;
  const bw = tw + 10;
  const bh = 17;
  const bx = Math.min(W - bw - 4, Math.max(4, x - bw / 2));

  ctx.fillStyle = 'rgba(15, 23, 42, 0.92)';
  ctx.strokeStyle = '#fbbf24';
  ctx.lineWidth = 1.2;
  ctx.beginPath();
  ctx.roundRect(bx, 3, bw, bh, 4);
  ctx.fill();
  ctx.stroke();

  ctx.fillStyle = '#fbbf24';
  ctx.textAlign = 'center';
  ctx.fillText(label, bx + bw / 2, 15);
}

// ============================================================================
// INTERAÇÃO COM O MOUSE: CLIQUE E ARRASTE NA TELA 2, TELA 3 E BARRA DE TEMPO
// ============================================================================
function setupGraphClickToSeek(canvasId) {
  const cv = document.getElementById(canvasId);
  let isDraggingGraph = false;

  function seekFromMouseEvent(e) {
    if (!rawSamplesGlobal || totalDurationSec <= 0) return;
    const rect = cv.getBoundingClientRect();
    const relX = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    const targetSec = relX * totalDurationSec;
    setSeekPosition(targetSec, true);
  }

  cv.addEventListener('mousedown', e => {
    if (!rawSamplesGlobal) return;
    isDraggingGraph = true;
    seekFromMouseEvent(e);
  });

  window.addEventListener('mousemove', e => {
    if (isDraggingGraph) seekFromMouseEvent(e);
  });

  window.addEventListener('mouseup', () => {
    isDraggingGraph = false;
  });
}

setupGraphClickToSeek('cTime');
setupGraphClickToSeek('cWaterfall');

function onTimelineSliderInput(sliderVal) {
  if (!rawSamplesGlobal || totalDurationSec <= 0) return;
  const ratio = parseFloat(sliderVal) / 1000.0;
  const targetSec = ratio * totalDurationSec;
  setSeekPosition(targetSec, true);
}

function setSeekPosition(targetSec, restartIfPlaying) {
  if (!rawSamplesGlobal || totalDurationSec <= 0) return;
  selectedSeekSec = Math.max(0, Math.min(totalDurationSec - 0.01, targetSec));
  currentPlayheadSec = selectedSeekSec;

  // Sincroniza slider e textos
  const slider = document.getElementById('timelineSlider');
  slider.value = Math.round((selectedSeekSec / totalDurationSec) * 1000);
  updateTimeCounterUI(selectedSeekSec);
  document.getElementById('timeInfoBadge').textContent =
    `Ponto Selecionado: ${selectedSeekSec.toFixed(2)}s / ${totalDurationSec.toFixed(2)}s`;

  drawTimeAndWaterfallWithPlayhead(selectedSeekSec);

  // Se o áudio já estiver tocando quando o usuário arrastar a barra ou clicar no gráfico, pula na hora!
  if (isPlayingAudio && restartIfPlaying) {
    startPlaybackFromOffset(activePlayMode, selectedSeekSec);
  }
}

function updateTimeCounterUI(curSec) {
  document.getElementById('timeCounter').textContent =
    `Cursor: ${curSec.toFixed(2)}s / ${totalDurationSec.toFixed(2)}s`;
}

// ============================================================================
// MOTOR DE AUDIFICAÇÃO COM PLAY A PARTIR DO TRECHO SELECIONADO
// ============================================================================
function initAudioContext() {
  if (!audioCtx) {
    audioCtx = new (window.AudioContext || window.webkitAudioContext)();
  }
  if (audioCtx.state === 'suspended') {
    audioCtx.resume();
  }
}

function highlightActiveModeButton(mode) {
  activePlayMode = mode;
  document.getElementById('btnPlayOrig').classList.toggle('active-mode', mode === 'orig');
  document.getElementById('btnPlayInfra').classList.toggle('active-mode', mode === 'infra');
  document.getElementById('btnPlayUltra').classList.toggle('active-mode', mode === 'ultra');
}

function selectAndPlayMode(mode) {
  if (!rawSamplesGlobal) return;
  highlightActiveModeButton(mode);
  // Se o cursor estiver muito no finalzinho, volta para 0.0s automaticamente
  if (selectedSeekSec >= totalDurationSec - 0.05) {
    selectedSeekSec = 0;
  }
  startPlaybackFromOffset(mode, selectedSeekSec);
}

function togglePlayFromSelectedPoint() {
  if (!rawSamplesGlobal) return;
  if (isPlayingAudio) {
    stopAudio(false); // Pausa mantendo a agulha onde estava
  } else {
    if (selectedSeekSec >= totalDurationSec - 0.05) {
      selectedSeekSec = 0;
    }
    startPlaybackFromOffset(activePlayMode, selectedSeekSec);
  }
}

function stopAudio(resetToSelected) {
  if (animFrameId) {
    cancelAnimationFrame(animFrameId);
    animFrameId = null;
  }
  if (currentSource) {
    currentSource.onended = null;
    try { currentSource.stop(); } catch (e) {}
    currentSource.disconnect();
    currentSource = null;
  }
  isPlayingAudio = false;
  document.getElementById('btnPlayFromCursor').textContent = "▶️ Tocar Trecho";

  if (resetToSelected) {
    currentPlayheadSec = selectedSeekSec;
  } else {
    selectedSeekSec = currentPlayheadSec;
  }
  updateTimeCounterUI(selectedSeekSec);
  const slider = document.getElementById('timelineSlider');
  if (totalDurationSec > 0) {
    slider.value = Math.round((selectedSeekSec / totalDurationSec) * 1000);
  }
  drawTimeAndWaterfallWithPlayhead(selectedSeekSec);
  document.getElementById('audioStatus').textContent =
    `⏸ Parado em ${selectedSeekSec.toFixed(2)}s — Aperte ▶️ Tocar Trecho para ouvir daqui`;
}

function startPlaybackFromOffset(mode, offsetSec) {
  if (!rawSamplesGlobal) return;
  if (animFrameId) cancelAnimationFrame(animFrameId);
  if (currentSource) {
    currentSource.onended = null;
    try { currentSource.stop(); } catch (e) {}
    currentSource.disconnect();
    currentSource = null;
  }

  initAudioContext();
  highlightActiveModeButton(mode);

  let bufferSamples = normOrigGlobal;
  let playbackRate = 1.0;
  let statusText = "";

  if (mode === 'orig') {
    bufferSamples = normOrigGlobal;
    playbackRate = 1.0;
    statusText = `🔊 Tocando Original (1x) a partir de ${offsetSec.toFixed(2)}s`;
    lastAudifiedSamples = normOrigGlobal;
    lastAudifiedRate = sampleRateGlobal;
    lastAudifiedName = "som_original_amplificado.wav";
  } else if (mode === 'infra') {
    // Em 25x, repete o trecho do ponto selecionado em diante para dar tempo de ouvir a onda grave
    const startIdx = Math.floor(offsetSec * sampleRateGlobal);
    const sliceLen = Math.max(512, normInfraGlobal.length - startIdx);
    const repeatCount = 6;
    const loopedInfra = new Float32Array(sliceLen * repeatCount);
    const subSlice = normInfraGlobal.subarray(startIdx);
    for (let r = 0; r < repeatCount; r++) {
      loopedInfra.set(subSlice, r * sliceLen);
    }
    bufferSamples = loopedInfra;
    playbackRate = 25.0;
    statusText = `🌋 Audificando Infrassom (25x) a partir de ${offsetSec.toFixed(2)}s`;

    const decimatedLen = Math.floor(loopedInfra.length / 8);
    const exportBuf = new Float32Array(decimatedLen);
    for (let i = 0; i < decimatedLen; i++) exportBuf[i] = loopedInfra[i * 8];
    lastAudifiedSamples = exportBuf;
    lastAudifiedRate = Math.round((sampleRateGlobal * playbackRate) / 8);
    lastAudifiedName = "infrassom_audificado_25x.wav";
  } else if (mode === 'ultra') {
    bufferSamples = normUltraGlobal;
    playbackRate = 1.0 / 6.0;
    statusText = `🦇 Audificando Ultrassom (÷6x) a partir de ${offsetSec.toFixed(2)}s`;

    const expandedLen = normUltraGlobal.length * 6;
    const expandedBuf = new Float32Array(expandedLen);
    for (let i = 0; i < normUltraGlobal.length - 1; i++) {
      const s0 = normUltraGlobal[i];
      const s1 = normUltraGlobal[i + 1];
      for (let k = 0; k < 6; k++) {
        expandedBuf[i * 6 + k] = s0 + (s1 - s0) * (k / 6.0);
      }
    }
    lastAudifiedSamples = expandedBuf;
    lastAudifiedRate = sampleRateGlobal;
    lastAudifiedName = "ultrassom_audificado_div6x.wav";
  }

  document.getElementById('btnDownloadAudified').disabled = false;

  const audioBuffer = audioCtx.createBuffer(1, bufferSamples.length, sampleRateGlobal);
  audioBuffer.copyToChannel(bufferSamples, 0);

  const source = audioCtx.createBufferSource();
  source.buffer = audioBuffer;
  source.playbackRate.value = playbackRate;

  const gainNode = audioCtx.createGain();
  gainNode.gain.value = manualGainMultiplier;

  const limiter = audioCtx.createDynamicsCompressor();
  limiter.threshold.value = -1.0;
  limiter.knee.value = 0.0;
  limiter.ratio.value = 20.0;
  limiter.attack.value = 0.001;
  limiter.release.value = 0.050;

  source.connect(gainNode);
  gainNode.connect(limiter);
  limiter.connect(audioCtx.destination);

  playStartWallTime = audioCtx.currentTime;
  playStartOffsetSec = offsetSec;
  currentPlaybackRate = playbackRate;
  isPlayingAudio = true;

  document.getElementById('btnPlayFromCursor').textContent = "⏸ Pausar";
  document.getElementById('audioStatus').textContent =
    `${statusText} [Ganho: ${manualGainMultiplier.toFixed(1)}x]`;

  source.onended = () => {
    if (!isPlayingAudio) return;
    isPlayingAudio = false;
    if (animFrameId) cancelAnimationFrame(animFrameId);
    currentPlayheadSec = selectedSeekSec;
    drawTimeAndWaterfallWithPlayhead(selectedSeekSec);
    updateTimeCounterUI(selectedSeekSec);
    document.getElementById('timelineSlider').value =
      Math.round((selectedSeekSec / totalDurationSec) * 1000);
    document.getElementById('btnPlayFromCursor').textContent = "▶️ Tocar Trecho";
    document.getElementById('audioStatus').textContent =
      `✅ Fim do trecho (Agulha pronta em ${selectedSeekSec.toFixed(2)}s para repetir)`;
  };

  // No modo 'infra', o buffer já foi fatiado a partir de offsetSec
  const bufferStartOffset = (mode === 'infra') ? 0 : offsetSec;
  source.start(0, bufferStartOffset);
  currentSource = source;
  currentGainNode = gainNode;

  // Loop de animação a 60 FPS para mover a agulha e o slider durante a reprodução
  function updatePlayheadLoop() {
    if (!isPlayingAudio || !audioCtx) return;
    const elapsedWall = audioCtx.currentTime - playStartWallTime;

    if (mode === 'infra') {
      const span = Math.max(0.05, totalDurationSec - playStartOffsetSec);
      currentPlayheadSec = playStartOffsetSec + ((elapsedWall * currentPlaybackRate) % span);
    } else {
      currentPlayheadSec = Math.min(totalDurationSec, playStartOffsetSec + elapsedWall * currentPlaybackRate);
    }

    drawTimeAndWaterfallWithPlayhead(currentPlayheadSec);
    updateTimeCounterUI(currentPlayheadSec);

    const slider = document.getElementById('timelineSlider');
    slider.value = Math.round((currentPlayheadSec / totalDurationSec) * 1000);

    animFrameId = requestAnimationFrame(updatePlayheadLoop);
  }

  animFrameId = requestAnimationFrame(updatePlayheadLoop);
}

function normalizeSignal(inputArray) {
  let maxPeak = 0.00001;
  for (let i = 0; i < inputArray.length; i++) {
    const a = Math.abs(inputArray[i]);
    if (a > maxPeak) maxPeak = a;
  }
  const out = new Float32Array(inputArray.length);
  const gain = 0.92 / maxPeak;
  for (let i = 0; i < inputArray.length; i++) {
    out[i] = inputArray[i] * gain;
  }
  return out;
}

// ============================================================================
// TELA 1: ESPECTRO FFT DE ALTA RESOLUÇÃO COM ETIQUETA NO PICO E MARCA D'ÁGUA
// ============================================================================
function drawFftSpectrumWithPeakBadge(mouseX) {
  if (!fftPlotState) return;
  const { dbSpectrum, halfFft, fs, fftSize, peakBin, peakFreq, peakDb, secondaryPeaks } = fftPlotState;

  const cFft = document.getElementById('cFft');
  const ctxF = cFft.getContext('2d');
  const W = cFft.width, H = cFft.height;
  const padTop = 34, padBottom = 22;
  const plotH = H - padTop - padBottom;
  const nyquist = fs / 2;

  ctxF.fillStyle = '#04070d';
  ctxF.fillRect(0, 0, W, H);

  const xInfra = Math.max(4, (20 / nyquist) * W);
  const xUltra = (11500 / nyquist) * W;

  ctxF.fillStyle = 'rgba(248, 113, 113, 0.08)';
  ctxF.fillRect(0, padTop, xInfra, plotH);
  ctxF.fillStyle = 'rgba(192, 132, 252, 0.06)';
  ctxF.fillRect(xUltra, padTop, W - xUltra, plotH);

  ctxF.strokeStyle = '#132038';
  ctxF.lineWidth = 1;
  ctxF.fillStyle = '#94a3b8';
  ctxF.font = '10.5px Consolas, monospace';
  ctxF.textAlign = 'center';

  const stepHz = 2500;
  for (let f = 0; f <= nyquist; f += stepHz) {
    const x = (f / nyquist) * W;
    ctxF.beginPath();
    ctxF.moveTo(x, padTop);
    ctxF.lineTo(x, padTop + plotH);
    ctxF.stroke();
    if (f > 0 && x < W - 25) {
      ctxF.fillText(`${(f / 1000).toFixed(1)}k`, x, H - 6);
    }
  }
  ctxF.textAlign = 'left';
  ctxF.fillText('0 Hz', 5, H - 6);

  for (let db = -20; db >= -80; db -= 20) {
    const norm = (db + 90) / 90;
    const y = padTop + plotH - norm * plotH;
    ctxF.beginPath();
    ctxF.moveTo(0, y);
    ctxF.lineTo(W, y);
    ctxF.stroke();
    ctxF.fillStyle = '#475569';
    ctxF.fillText(`${db} dB`, 6, y - 3);
  }

  // Marca d'Água Científica Oficial no Canto Superior Direito do Espectro
  ctxF.fillStyle = 'rgba(251, 191, 36, 0.78)';
  ctxF.font = 'bold 11.5px Segoe UI, sans-serif';
  ctxF.textAlign = 'right';
  ctxF.fillText('🔬 Laboratório Acústico Científico • Autor: Robson Araujo', W - 12, 18);

  ctxF.beginPath();
  for (let k = 1; k < halfFft; k++) {
    const x = (k / halfFft) * W;
    const norm = Math.max(0, Math.min(1, (dbSpectrum[k] + 90) / 90));
    const y = padTop + plotH - norm * plotH;
    if (k === 1) ctxF.moveTo(x, y);
    else ctxF.lineTo(x, y);
  }
  ctxF.strokeStyle = '#38bdf8';
  ctxF.lineWidth = 1.6;
  ctxF.stroke();

  ctxF.lineTo(W, padTop + plotH);
  ctxF.lineTo(0, padTop + plotH);
  ctxF.closePath();
  const grad = ctxF.createLinearGradient(0, padTop, 0, padTop + plotH);
  grad.addColorStop(0, 'rgba(56, 189, 248, 0.30)');
  grad.addColorStop(1, 'rgba(56, 189, 248, 0.01)');
  ctxF.fillStyle = grad;
  ctxF.fill();

  for (const sp of secondaryPeaks) {
    const sx = (sp.bin / halfFft) * W;
    const sNorm = Math.max(0, Math.min(1, (sp.db + 90) / 90));
    const sy = padTop + plotH - sNorm * plotH;

    ctxF.fillStyle = '#4ade80';
    ctxF.beginPath();
    ctxF.arc(sx, sy, 3.5, 0, Math.PI * 2);
    ctxF.fill();

    ctxF.font = 'bold 10px Consolas, monospace';
    ctxF.textAlign = 'center';
    const labelSec = sp.freq >= 1000 ? `${(sp.freq).toFixed(0)} Hz` : `${sp.freq.toFixed(1)} Hz`;
    ctxF.fillText(labelSec, Math.min(W - 35, Math.max(35, sx)), Math.max(padTop + 12, sy - 7));
  }

  // Etiqueta principal cravada no pico da onda
  const px = (peakBin / halfFft) * W;
  const pNorm = Math.max(0, Math.min(1, (peakDb + 90) / 90));
  const py = padTop + plotH - pNorm * plotH;

  ctxF.save();
  ctxF.setLineDash([4, 4]);
  ctxF.strokeStyle = 'rgba(251, 191, 36, 0.7)';
  ctxF.lineWidth = 1.3;
  ctxF.beginPath();
  ctxF.moveTo(px, py);
  ctxF.lineTo(px, padTop + plotH);
  ctxF.stroke();
  ctxF.restore();

  ctxF.fillStyle = '#fbbf24';
  ctxF.beginPath();
  ctxF.arc(px, py, 5, 0, Math.PI * 2);
  ctxF.fill();
  ctxF.strokeStyle = '#04070d';
  ctxF.lineWidth = 1.5;
  ctxF.stroke();

  const peakText = `▼ PICO: ${peakFreq.toFixed(1)} Hz (${peakDb.toFixed(1)} dB)`;
  ctxF.font = 'bold 11.5px Segoe UI, sans-serif';
  const textWidth = ctxF.measureText(peakText).width;
  const boxW = textWidth + 16;
  const boxH = 22;
  const boxX = Math.min(W - boxW - 6, Math.max(6, px - boxW / 2));
  const boxY = Math.max(4, py - boxH - 9);

  ctxF.fillStyle = 'rgba(15, 23, 42, 0.94)';
  ctxF.strokeStyle = '#fbbf24';
  ctxF.lineWidth = 1.5;
  ctxF.beginPath();
  ctxF.roundRect(boxX, boxY, boxW, boxH, 5);
  ctxF.fill();
  ctxF.stroke();

  ctxF.fillStyle = '#fbbf24';
  ctxF.textAlign = 'center';
  ctxF.fillText(peakText, boxX + boxW / 2, boxY + 15);

  if (mouseX !== null && mouseX >= 0 && mouseX <= W) {
    const binHover = Math.min(halfFft - 1, Math.max(1, Math.round((mouseX / W) * halfFft)));
    const freqHover = (binHover * fs) / fftSize;
    const dbHover = dbSpectrum[binHover];
    const hNorm = Math.max(0, Math.min(1, (dbHover + 90) / 90));
    const hy = padTop + plotH - hNorm * plotH;

    ctxF.strokeStyle = 'rgba(255, 255, 255, 0.35)';
    ctxF.lineWidth = 1;
    ctxF.beginPath();
    ctxF.moveTo(mouseX, padTop);
    ctxF.lineTo(mouseX, padTop + plotH);
    ctxF.stroke();

    ctxF.fillStyle = '#ffffff';
    ctxF.beginPath();
    ctxF.arc(mouseX, hy, 3.5, 0, Math.PI * 2);
    ctxF.fill();

    document.getElementById('fftCursorInfo').textContent =
      `Cursor: ${freqHover.toFixed(1)} Hz (${dbHover.toFixed(1)} dB)  |  Pico Principal: ${peakFreq.toFixed(1)} Hz`;
  } else {
    document.getElementById('fftCursorInfo').textContent =
      `Pico Dominante: ${peakFreq.toFixed(1)} Hz (${peakDb.toFixed(1)} dB) — Passe o mouse para inspecionar`;
  }
}

const cFftElem = document.getElementById('cFft');
cFftElem.addEventListener('mousemove', e => {
  if (!fftPlotState) return;
  const rect = cFftElem.getBoundingClientRect();
  const scaleX = cFftElem.width / rect.width;
  const mouseX = (e.clientX - rect.left) * scaleX;
  drawFftSpectrumWithPeakBadge(mouseX);
});
cFftElem.addEventListener('mouseleave', () => {
  if (!fftPlotState) return;
  drawFftSpectrumWithPeakBadge(null);
});

function thermalColorMap(v) {
  const x = Math.floor(v * 255);
  if (x < 64) return [0, 0, x * 4];
  if (x < 128) return [0, (x - 64) * 4, 255];
  if (x < 192) return [(x - 128) * 4, 255, 255 - (x - 128) * 4];
  return [255, 255 - (x - 192) * 4, 0];
}

function downloadLastAudifiedWav() {
  if (!lastAudifiedSamples) return;
  const numSamples = lastAudifiedSamples.length;
  const buffer = new ArrayBuffer(44 + numSamples * 2);
  const view = new DataView(buffer);

  function writeStr(offset, str) {
    for (let i = 0; i < str.length; i++) view.setUint8(offset + i, str.charCodeAt(i));
  }

  writeStr(0, 'RIFF');
  view.setUint32(4, 36 + numSamples * 2, true);
  writeStr(8, 'WAVE');
  writeStr(12, 'fmt ');
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, 1, true);
  view.setUint32(24, lastAudifiedRate, true);
  view.setUint32(28, lastAudifiedRate * 2, true);
  view.setUint16(32, 2, true);
  view.setUint16(34, 16, true);
  writeStr(36, 'data');
  view.setUint32(40, numSamples * 2, true);

  for (let i = 0; i < numSamples; i++) {
    const amplified = Math.tanh(lastAudifiedSamples[i] * manualGainMultiplier);
    const s = Math.max(-1, Math.min(1, amplified));
    view.setInt16(44 + i * 2, s < 0 ? s * 0x8000 : s * 0x7FFF, true);
  }

  const blob = new Blob([buffer], { type: 'audio/wav' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = lastAudifiedName;
  a.click();
  URL.revokeObjectURL(url);
}

// ============================================================================
// FFT RADIX-2 COOLEY-TUKEY IN-PLACE
// ============================================================================
function fftRadix2(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      let tr = re[i]; re[i] = re[j]; re[j] = tr;
      let ti = im[i]; im[i] = im[j]; im[i] = ti;
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len;
    const wlen_r = Math.cos(ang), wlen_i = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let wr = 1, wi = 0;
      for (let j = 0; j < len / 2; j++) {
        const u_r = re[i + j], u_i = im[i + j];
        const v_r = re[i + j + len / 2] * wr - im[i + j + len / 2] * wi;
        const v_i = re[i + j + len / 2] * wi + im[i + j + len / 2] * wr;
        re[i + j] = u_r + v_r; im[i + j] = u_i + v_i;
        re[i + j + len / 2] = u_r - v_r; im[i + j + len / 2] = u_i - v_i;
        const nwr = wr * wlen_r - wi * wlen_i;
        wi = wr * wlen_i + wi * wlen_r;
        wr = nwr;
      }
    }
  }
}

window.addEventListener('DOMContentLoaded', drawInitialEmptyScreens);
