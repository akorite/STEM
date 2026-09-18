/**
 * KEBUN BIOSFER // MODERN PASTEL BIOSPHERE CONSOLE ENGINE
 * Delightful Modern Pastel Botanical Sanctuary & Biosphere Console
 * Production-Grade Instrument Logic • Zero Dependencies
 */

(function () {
  'use strict';

  // System State
  let telemetry = null;
  let pollTimer = null;
  let isSubmitting = false;
  let hasUncommittedSetpoints = false;
  let mascotInteractionTimer = null;

  // Resolve Target API Base
  function getApiBase() {
    const saved = localStorage.getItem('kb_api_base');
    if (saved !== null && saved !== undefined) return saved;
    if (window.location.protocol === 'file:') {
      return 'http://127.0.0.1:8080';
    }
    return window.location.origin;
  }

  function setApiBase(url) {
    let clean = (url || '').trim().replace(/\/+$/, '');
    if (clean && !/^https?:\/\//i.test(clean)) {
      clean = 'http://' + clean;
    }
    localStorage.setItem('kb_api_base', clean);
    return clean;
  }

  let apiBase = getApiBase();

  // Cached DOM Nodes
  const el = {
    liveDot: document.getElementById('liveDot'),
    systemStatusBadge: document.getElementById('systemStatusBadge'),
    btnAutoMode: document.getElementById('btnAutoMode'),
    lblAutoMode: document.getElementById('lblAutoMode'),
    btnPhase: document.getElementById('btnPhase'),
    lblPhase: document.getElementById('lblPhase'),
    btnDayDec: document.getElementById('btnDayDec'),
    btnDayInc: document.getElementById('btnDayInc'),
    dayDisplay: document.getElementById('dayDisplay'),
    targetHostInput: document.getElementById('targetHostInput'),
    btnConnectTarget: document.getElementById('btnConnectTarget'),
    ipDisplay: document.getElementById('ipDisplay'),
    uptimeDisplay: document.getElementById('uptimeDisplay'),

    // Atmospheric Readings
    valTemp: document.getElementById('valTemp'),
    needleTemp: document.getElementById('needleTemp'),
    subTemp: document.getElementById('subTemp'),
    tagTemp: document.getElementById('tagTemp'),

    valRh: document.getElementById('valRh'),
    needleRh: document.getElementById('needleRh'),
    subRh: document.getElementById('subRh'),
    tagRh: document.getElementById('tagRh'),

    valVpd: document.getElementById('valVpd'),
    needleVpd: document.getElementById('needleVpd'),
    subVpd: document.getElementById('subVpd'),
    tagVpd: document.getElementById('tagVpd'),

    // Biomass Readings & Cute Mascot
    valSoil: document.getElementById('valSoil'),
    needleSoil: document.getElementById('needleSoil'),
    subSoil: document.getElementById('subSoil'),
    tagSoil: document.getElementById('tagSoil'),

    valPcs: document.getElementById('valPcs'),
    fillPcs: document.getElementById('fillPcs'),
    subPcs: document.getElementById('subPcs'),
    tagPcs: document.getElementById('tagPcs'),
    sproutMascot: document.getElementById('sproutMascot'),
    mascotMoodTag: document.getElementById('mascotMoodTag'),
    mascotEyeL: document.getElementById('mascotEyeL'),
    mascotEyeR: document.getElementById('mascotEyeR'),
    mascotCheekL: document.getElementById('mascotCheekL'),
    mascotCheekR: document.getElementById('mascotCheekR'),
    mascotMouth: document.getElementById('mascotMouth'),

    // Fluidics
    activeTankLabel: document.getElementById('activeTankLabel'),
    valTank1: document.getElementById('valTank1'),
    fillTank1: document.getElementById('fillTank1'),
    valTank2: document.getElementById('valTank2'),
    fillTank2: document.getElementById('fillTank2'),
    btnToggleTank: document.getElementById('btnToggleTank'),

    // Strip-Chart
    telemetrySvg: document.getElementById('telemetrySvg'),
    chartWrap: document.getElementById('chartWrap'),
    tempLinePath: document.getElementById('tempLinePath'),
    rhLinePath: document.getElementById('rhLinePath'),
    tempAreaPath: document.getElementById('tempAreaPath'),
    rhAreaPath: document.getElementById('rhAreaPath'),
    tempPoints: document.getElementById('tempPoints'),
    rhPoints: document.getElementById('rhPoints'),
    chartTargetBands: document.getElementById('chartTargetBands'),
    chartGridLines: document.getElementById('chartGridLines'),
    chartAxesLabels: document.getElementById('chartAxesLabels'),
    chartStats: document.getElementById('chartStats'),

    // Setpoints & Calibration
    inputTempMin: document.getElementById('inputTempMin'),
    inputTempMax: document.getElementById('inputTempMax'),
    inputRhMin: document.getElementById('inputRhMin'),
    inputRhMax: document.getElementById('inputRhMax'),
    inputSoilMin: document.getElementById('inputSoilMin'),
    inputSoilMax: document.getElementById('inputSoilMax'),
    previewTemp: document.getElementById('previewTemp'),
    previewRh: document.getElementById('previewRh'),
    previewSoil: document.getElementById('previewSoil'),
    btnSyncThresholds: document.getElementById('btnSyncThresholds'),
    calibRawLabel: document.getElementById('calibRawLabel'),
    btnCalibDry: document.getElementById('btnCalibDry'),
    btnCalibWet: document.getElementById('btnCalibWet'),

    // Hardware Actuators
    btnEmergencyFlush: document.getElementById('btnEmergencyFlush'),
    relays: {
      1: { card: document.getElementById('relay1'), btn: document.getElementById('btnRelay1') },
      2: { card: document.getElementById('relay2'), btn: document.getElementById('btnRelay2') },
      3: { card: document.getElementById('relay3'), btn: document.getElementById('btnRelay3') },
      4: { card: document.getElementById('relay4'), btn: document.getElementById('btnRelay4') },
      5: { card: document.getElementById('relay5'), btn: document.getElementById('btnRelay5') },
      6: { card: document.getElementById('relay6'), btn: document.getElementById('btnRelay6') },
    },
    rangeValve1: document.getElementById('rangeValve1'),
    valValve1: document.getElementById('valValve1'),
    rangeValve2: document.getElementById('rangeValve2'),
    valValve2: document.getElementById('valValve2'),

    // Audit Log
    terminalLogs: document.getElementById('terminalLogs'),
    btnClearLogs: document.getElementById('btnClearLogs')
  };

  el.targetHostInput.value = apiBase;

  // Send Control Request
  async function sendControl(query) {
    try {
      const url = `${apiBase}/api/control?${query}`;
      const res = await fetch(url, { method: 'POST' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      fetchTelemetry();
    } catch (err) {
      appendAuditEntry(`Control command failed (${query}): ${err.message}`, 'crit');
    }
  }

  // Telemetry Acquisition Loop
  async function fetchTelemetry() {
    try {
      const res = await fetch(`${apiBase}/api/telemetry?_t=${Date.now()}`);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      telemetry = data;
      renderDashboard(data);
      el.liveDot.style.background = 'var(--mint-light)';
      el.liveDot.style.boxShadow = '0 0 0 3px rgba(16, 185, 129, 0.25)';
    } catch (err) {
      el.liveDot.style.background = 'var(--sakura-rose)';
      el.liveDot.style.boxShadow = '0 0 0 3px rgba(225, 29, 72, 0.25)';
      el.systemStatusBadge.className = 'status-tag status-kritis';
      el.systemStatusBadge.textContent = 'LINK TIMEOUT ⚠️';
    }
  }

  // Update Setpoint Range Previews
  function updateSetpointPreviews() {
    hasUncommittedSetpoints = true;
    if (el.btnSyncThresholds) el.btnSyncThresholds.classList.add('dirty');
    const tMin = parseFloat(el.inputTempMin.value) || 18.0;
    const tMax = parseFloat(el.inputTempMax.value) || 22.0;
    const rMin = parseFloat(el.inputRhMin.value) || 50.0;
    const rMax = parseFloat(el.inputRhMax.value) || 65.0;
    const sMin = parseFloat(el.inputSoilMin.value) || 45.0;
    const sMax = parseFloat(el.inputSoilMax.value) || 70.0;

    el.previewTemp.textContent = `${tMin.toFixed(1)}°C - ${tMax.toFixed(1)}°C`;
    el.previewRh.textContent = `${rMin.toFixed(1)}% - ${rMax.toFixed(1)}%`;
    el.previewSoil.textContent = `${sMin.toFixed(1)}% - ${sMax.toFixed(1)}%`;
  }

  // Render Telemetric Instruments
  function renderDashboard(d) {
    // 1. System Navigation
    const status = d.status || 'OPTIMAL';
    let statusText = 'NOMINAL ✨';
    let statusClass = 'status-nominal';

    if (status === 'KRITIS') {
      statusText = 'KRITIS 🚨';
      statusClass = 'status-kritis';
    } else if (status === 'WASPADA') {
      statusText = 'WASPADA ⚠️';
      statusClass = 'status-waspada';
    }

    el.systemStatusBadge.textContent = statusText;
    el.systemStatusBadge.className = `status-tag ${statusClass}`;

    el.lblAutoMode.textContent = d.autoMode ? 'CLOSED-LOOP AUTO' : 'MANUAL OVERRIDE';
    el.btnAutoMode.className = 'mode-toggle-btn ' + (d.autoMode ? 'active' : '');

    el.lblPhase.textContent = d.phase === 1 ? 'PHASE 1: GERMINATION' : 'PHASE 2: AUTOTROPHIC';
    el.btnPhase.className = 'phase-toggle-btn ' + (d.phase === 2 ? 'active' : '');

    const dayPad = String(d.day || 1).padStart(2, '0');
    el.dayDisplay.textContent = `DAY ${dayPad} / 10`;
    el.ipDisplay.textContent = d.ip || '127.0.0.1:8080';
    el.uptimeDisplay.textContent = d.uptime || '00:00:00';

    // 2. Atmospheric Thermodynamics Station
    // Air Temperature
    const temp = d.temp !== undefined ? d.temp : 20.0;
    el.valTemp.textContent = temp.toFixed(1);
    const tempPct = Math.min(100, Math.max(0, ((temp - 15.0) / (28.0 - 15.0)) * 100));
    el.needleTemp.style.left = `${tempPct}%`;
    const tempDelta = (temp - 20.0).toFixed(1);
    el.subTemp.textContent = `Setpoint: 20.0°C (Δ ${tempDelta > 0 ? '+' : ''}${tempDelta}°C)`;

    if (temp < 18.0) {
      el.tagTemp.textContent = 'CHILL HAZARD';
      el.tagTemp.className = 'state-pill state-warn';
    } else if (temp > 22.0) {
      el.tagTemp.textContent = temp > 24.0 ? 'PYTHIUM RISK' : 'THERMAL ELEVATION';
      el.tagTemp.className = 'state-pill ' + (temp > 24.0 ? 'state-crit' : 'state-warn');
    } else {
      el.tagTemp.textContent = 'NOMINAL';
      el.tagTemp.className = 'state-pill state-good';
    }

    // Relative Humidity
    const rh = d.rh !== undefined ? d.rh : 57.5;
    el.valRh.textContent = rh.toFixed(1);
    const rhPct = Math.min(100, Math.max(0, ((rh - 30.0) / (90.0 - 30.0)) * 100));
    el.needleRh.style.left = `${rhPct}%`;
    el.subRh.textContent = `Target: 57.5% (Band: 50-65%)`;

    if (rh < 50.0) {
      el.tagRh.textContent = 'VAPOR DEFICIENT';
      el.tagRh.className = 'state-pill state-warn';
    } else if (rh > 65.0) {
      el.tagRh.textContent = rh > 75.0 ? 'SATURATED AIR' : 'ELEVATED MOISTURE';
      el.tagRh.className = 'state-pill ' + (rh > 75.0 ? 'state-crit' : 'state-warn');
    } else {
      el.tagRh.textContent = 'STABLE';
      el.tagRh.className = 'state-pill state-good';
    }

    // Vapor Pressure Deficit
    const vpd = d.vpd !== undefined ? d.vpd : 0.60;
    el.valVpd.textContent = vpd.toFixed(2);
    const vpdPct = Math.min(100, Math.max(0, ((vpd - 0.1) / (1.5 - 0.1)) * 100));
    el.needleVpd.style.left = `${vpdPct}%`;
    el.subVpd.textContent = `VPsat ${(d.vpSat || 0).toFixed(2)} / VPact ${(d.vpAct || 0).toFixed(2)}`;

    if (vpd < 0.40) {
      el.tagVpd.textContent = 'STAGNANT';
      el.tagVpd.className = 'state-pill state-warn';
    } else if (vpd > 0.80) {
      el.tagVpd.textContent = 'STOMATAL STRESS';
      el.tagVpd.className = 'state-pill state-warn';
    } else {
      el.tagVpd.textContent = 'IDEAL TRANSPIRATION';
      el.tagVpd.className = 'state-pill state-good';
    }

    // 3. Substrate & Biomass Health Station
    const soil = d.soil !== undefined ? d.soil : 55.0;
    el.valSoil.textContent = soil.toFixed(1);
    const soilPct = Math.min(100, Math.max(0, ((soil - 20.0) / (90.0 - 20.0)) * 100));
    el.needleSoil.style.left = `${soilPct}%`;
    el.subSoil.textContent = `Capacitive: ${d.rawSoilAdc || '--'} ADC (${(d.soilVoltage || 0).toFixed(2)}V)`;

    if (soil < 45.0) {
      el.tagSoil.textContent = soil < 35.0 ? 'PARCHED' : 'LOW MOISTURE';
      el.tagSoil.className = 'state-pill ' + (soil < 35.0 ? 'state-crit' : 'state-warn');
    } else if (soil > 70.0) {
      el.tagSoil.textContent = 'OVERSATURATED';
      el.tagSoil.className = 'state-pill state-warn';
    } else {
      el.tagSoil.textContent = 'HYDRATED';
      el.tagSoil.className = 'state-pill state-good';
    }

    const pcs = d.pcs !== undefined ? d.pcs : 95.0;
    el.valPcs.textContent = pcs.toFixed(1);
    el.fillPcs.style.transform = `scaleX(${Math.min(1, Math.max(0, pcs / 100))})`;

    if (pcs < 70.0) {
      el.tagPcs.textContent = pcs < 50.0 ? 'CRITICAL STRESS' : 'SUB-OPTIMAL';
      el.tagPcs.className = 'state-pill ' + (pcs < 50.0 ? 'state-crit' : 'state-warn');
    } else {
      el.tagPcs.textContent = 'OPTIMAL';
      el.tagPcs.className = 'state-pill state-good';
    }

    // Mascot Emotional Reaction Logic
    updateMascotEmotion(pcs, d);

    // 4. Fluidics Reservoirs Station
    el.activeTankLabel.textContent = `TANK ${d.activeTank || 1} ACTIVE`;
    const t1 = d.tank1Vol !== undefined ? d.tank1Vol : 800;
    const t2 = d.tank2Vol !== undefined ? d.tank2Vol : 800;
    el.valTank1.textContent = `${Math.round(t1)} mL`;
    el.valTank2.textContent = `${Math.round(t2)} mL`;
    el.fillTank1.style.transform = `scaleX(${Math.min(1, Math.max(0, t1 / 1000))})`;
    el.fillTank2.style.transform = `scaleX(${Math.min(1, Math.max(0, t2 / 1000))})`;

    // 5. Environmental Strip-Chart
    if (d.history && d.history.temp && d.history.rh) {
      renderStripChart(d.history.temp, d.history.rh);
    }

    // 6. Setpoints & Calibrations
    if (d.thresholds && !isSubmitting && !hasUncommittedSetpoints) {
      el.previewTemp.textContent = `${d.thresholds.tempMin.toFixed(1)}°C - ${d.thresholds.tempMax.toFixed(1)}°C`;
      el.previewRh.textContent = `${d.thresholds.rhMin.toFixed(1)}% - ${d.thresholds.rhMax.toFixed(1)}%`;
      el.previewSoil.textContent = `${d.thresholds.soilMin.toFixed(1)}% - ${d.thresholds.soilMax.toFixed(1)}%`;

      if (document.activeElement !== el.inputTempMin && document.activeElement !== el.inputTempMax) {
        el.inputTempMin.value = d.thresholds.tempMin;
        el.inputTempMax.value = d.thresholds.tempMax;
      }
      if (document.activeElement !== el.inputRhMin && document.activeElement !== el.inputRhMax) {
        el.inputRhMin.value = d.thresholds.rhMin;
        el.inputRhMax.value = d.thresholds.rhMax;
      }
      if (document.activeElement !== el.inputSoilMin && document.activeElement !== el.inputSoilMax) {
        el.inputSoilMin.value = d.thresholds.soilMin;
        el.inputSoilMax.value = d.thresholds.soilMax;
      }
    }

    if (d.rawSoilAdc !== undefined) {
      el.calibRawLabel.textContent = `ADC: ${d.rawSoilAdc} (${(d.soilVoltage || 0).toFixed(2)}V)`;
    }

    // 7. Hardware Relays
    if (d.relays) {
      updateRelayState(1, d.relays.peltier);
      updateRelayState(2, d.relays.fan);
      updateRelayState(3, d.relays.blower);
      updateRelayState(4, d.relays.light);
      updateRelayState(5, d.relays.spray1);
      updateRelayState(6, d.relays.spray2);
    }

    // 8. Servos
    if (d.valves) {
      if (document.activeElement !== el.rangeValve1) {
        el.rangeValve1.value = d.valves.v1;
        el.valValve1.textContent = `${d.valves.v1}°`;
      }
      if (document.activeElement !== el.rangeValve2) {
        el.rangeValve2.value = d.valves.v2;
        el.valValve2.textContent = `${d.valves.v2}°`;
      }
    }

    // 9. Chronological Audit Log
    if (d.logs && Array.isArray(d.logs)) {
      renderAuditEntries(d.logs);
    }
  }

  // Cute Brocco Mascot Emotional Reaction
  function updateMascotEmotion(pcs, d) {
    if (!el.mascotMoodTag || mascotInteractionTimer) return;
    // Night rest if light is explicitly off
    if (d.relays && d.relays.light === false) {
      el.mascotMoodTag.textContent = 'Resting 🌙';
      if (el.mascotEyeL && el.mascotEyeR) {
        el.mascotEyeL.setAttribute('ry', '0.6');
        el.mascotEyeR.setAttribute('ry', '0.6');
      }
      if (el.mascotMouth) {
        el.mascotMouth.setAttribute('d', 'M47.5 25.5 Q50 27.5 52.5 25.5');
      }
      return;
    }

    // Active daytime moods
    if (pcs >= 85.0) {
      el.mascotMoodTag.textContent = 'Thriving ✨';
      if (el.mascotEyeL && el.mascotEyeR) {
        el.mascotEyeL.setAttribute('ry', '2.8');
        el.mascotEyeR.setAttribute('ry', '2.8');
      }
      if (el.mascotCheekL && el.mascotCheekR) {
        el.mascotCheekL.setAttribute('opacity', '0.85');
        el.mascotCheekR.setAttribute('opacity', '0.85');
      }
      if (el.mascotMouth) {
        el.mascotMouth.setAttribute('d', 'M47.5 24.5 Q50 28.5 52.5 24.5');
      }
    } else if (pcs >= 70.0) {
      el.mascotMoodTag.textContent = 'Cozy 🌱';
      if (el.mascotEyeL && el.mascotEyeR) {
        el.mascotEyeL.setAttribute('ry', '2.4');
        el.mascotEyeR.setAttribute('ry', '2.4');
      }
      if (el.mascotMouth) {
        el.mascotMouth.setAttribute('d', 'M47.5 25 Q50 27.5 52.5 25');
      }
    } else if (pcs >= 50.0) {
      el.mascotMoodTag.textContent = 'A bit stressed 🥺';
      if (el.mascotEyeL && el.mascotEyeR) {
        el.mascotEyeL.setAttribute('ry', '2.0');
        el.mascotEyeR.setAttribute('ry', '2.0');
      }
      if (el.mascotMouth) {
        el.mascotMouth.setAttribute('d', 'M48 26.5 L52 26.5');
      }
    } else {
      el.mascotMoodTag.textContent = 'Needs Help! 💦';
      if (el.mascotEyeL && el.mascotEyeR) {
        el.mascotEyeL.setAttribute('ry', '1.6');
        el.mascotEyeR.setAttribute('ry', '1.6');
      }
      if (el.mascotMouth) {
        el.mascotMouth.setAttribute('d', 'M47.5 27.5 Q50 24.5 52.5 27.5');
      }
    }
  }

  function updateRelayState(ch, isOn) {
    const r = el.relays[ch];
    if (!r) return;
    if (isOn) {
      r.card.classList.add('active');
      r.btn.setAttribute('aria-pressed', 'true');
    } else {
      r.card.classList.remove('active');
      r.btn.setAttribute('aria-pressed', 'false');
    }
  }

  // Precision Strip-Chart Vector Plotter
  function renderStripChart(tempArr, rhArr) {
    if (!tempArr || tempArr.length === 0) return;

    const w = 840;
    const h = 220;
    const padL = 36;
    const padR = 36;
    const padT = 16;
    const padB = 24;
    const plotW = w - padL - padR;
    const plotH = h - padT - padB;

    const minTemp = 16.0;
    const maxTemp = 26.0;
    const minRh = 40.0;
    const maxRh = 80.0;

    const n = tempArr.length;
    const stepX = n > 1 ? plotW / (n - 1) : plotW;

    const tempCoords = [];
    const rhCoords = [];

    let sumT = 0, minT = 999, maxT = -999;
    let sumR = 0, minR = 999, maxR = -999;

    for (let i = 0; i < n; i++) {
      const t = tempArr[i];
      const r = rhArr[i];

      sumT += t;
      if (t < minT) minT = t;
      if (t > maxT) maxT = t;

      sumR += r;
      if (r < minR) minR = r;
      if (r > maxR) maxR = r;

      const x = padL + i * stepX;
      const yT = padT + plotH - ((t - minTemp) / (maxTemp - minTemp)) * plotH;
      const yR = padT + plotH - ((r - minRh) / (maxRh - minRh)) * plotH;

      tempCoords.push({ x, y: yT, val: t });
      rhCoords.push({ x, y: yR, val: r });
    }

    const avgT = (sumT / n).toFixed(1);
    const avgR = (sumR / n).toFixed(1);
    el.chartStats.textContent = `T: ${minT.toFixed(1)}-${maxT.toFixed(1)}°C (Avg ${avgT}°) | RH: ${minR.toFixed(1)}-${maxR.toFixed(1)}% (Avg ${avgR}%)`;

    const makePath = (coords) => coords.map((c, i) => `${i === 0 ? 'M' : 'L'} ${c.x.toFixed(1)} ${c.y.toFixed(1)}`).join(' ');
    const makeArea = (coords) => {
      const line = makePath(coords);
      const lastX = coords[coords.length - 1].x.toFixed(1);
      const firstX = coords[0].x.toFixed(1);
      const bottomY = (padT + plotH).toFixed(1);
      return `${line} L ${lastX} ${bottomY} L ${firstX} ${bottomY} Z`;
    };

    el.tempLinePath.setAttribute('d', makePath(tempCoords));
    el.tempAreaPath.setAttribute('d', makeArea(tempCoords));

    el.rhLinePath.setAttribute('d', makePath(rhCoords));
    el.rhAreaPath.setAttribute('d', makeArea(rhCoords));

    // Target Agronomic Ribbon (18.0 - 22.0°C)
    const optTMinY = padT + plotH - ((18.0 - minTemp) / (maxTemp - minTemp)) * plotH;
    const optTMaxY = padT + plotH - ((22.0 - minTemp) / (maxTemp - minTemp)) * plotH;
    const ribbonH = Math.abs(optTMinY - optTMaxY);

    el.chartTargetBands.innerHTML = `
      <rect x="${padL}" y="${Math.min(optTMinY, optTMaxY)}" width="${plotW}" height="${ribbonH}" fill="url(#targetZonePattern)" stroke="rgba(16, 185, 129, 0.4)" stroke-dasharray="3,3"/>
    `;

    // Horizontal Calibration Grid Lines & Labels
    let gridHtml = '';
    let axesHtml = '';
    const steps = 4;
    for (let i = 0; i <= steps; i++) {
      const y = padT + (plotH / steps) * i;
      const tVal = (maxTemp - (i / steps) * (maxTemp - minTemp)).toFixed(1);
      const rVal = Math.round(maxRh - (i / steps) * (maxRh - minRh));

      gridHtml += `<line x1="${padL}" y1="${y}" x2="${padL + plotW}" y2="${y}" stroke="rgba(226, 232, 240, 0.85)" stroke-width="1" stroke-dasharray="2,3"/>`;
      axesHtml += `
        <text x="${padL - 6}" y="${y + 3}" fill="#059669" font-size="9" font-family="monospace" font-weight="700" text-anchor="end">${tVal}°</text>
        <text x="${padL + plotW + 6}" y="${y + 3}" fill="#0284c7" font-size="9" font-family="monospace" font-weight="700" text-anchor="start">${rVal}%</text>
      `;
    }
    el.chartGridLines.innerHTML = gridHtml;
    el.chartAxesLabels.innerHTML = axesHtml;

    // Real-Time End Points with soft glowing borders
    const latestT = tempCoords[tempCoords.length - 1];
    const latestR = rhCoords[rhCoords.length - 1];
    el.tempPoints.innerHTML = `<circle cx="${latestT.x}" cy="${latestT.y}" r="4" fill="#10b981" stroke="#ffffff" stroke-width="2"/>`;
    el.rhPoints.innerHTML = `<circle cx="${latestR.x}" cy="${latestR.y}" r="4" fill="#0284c7" stroke="#ffffff" stroke-width="2"/>`;
  }

  // Chronological Audit Stream
  let lastLogsSignature = '';
  function renderAuditEntries(logs) {
    const sig = JSON.stringify(logs);
    if (sig === lastLogsSignature) return;
    lastLogsSignature = sig;

    el.terminalLogs.innerHTML = '';
    logs.forEach(item => {
      const row = document.createElement('div');
      row.className = 'audit-entry';

      let tag = 'SYS';
      let tagClass = 'tag-sys';

      if (/interlock|mati|padam/i.test(item.m)) {
        tag = 'LOCK';
        tagClass = 'tag-lock';
      } else if (/critical|kritis|danger|fault/i.test(item.m)) {
        tag = 'CRIT';
        tagClass = 'tag-crit';
      } else if (/manual|override|klik/i.test(item.m)) {
        tag = 'MANUAL';
        tagClass = 'tag-man';
      } else if (/auto|siklus|stabil|optimal/i.test(item.m)) {
        tag = 'AUTO';
        tagClass = 'tag-auto';
      }

      row.innerHTML = `
        <span class="audit-time">[${escapeHtml(item.t)}]</span>
        <span class="audit-tag ${tagClass}">[${tag}]</span>
        <span class="audit-msg">${escapeHtml(item.m)}</span>
      `;
      el.terminalLogs.appendChild(row);
    });

    el.terminalLogs.scrollTop = el.terminalLogs.scrollHeight;
  }

  function appendAuditEntry(msg, tagKey) {
    const now = new Date().toTimeString().split(' ')[0];
    const row = document.createElement('div');
    row.className = 'audit-entry';
    const tag = (tagKey || 'SYS').toUpperCase();
    const tagClass = `tag-${(tagKey || 'sys').toLowerCase()}`;

    row.innerHTML = `
      <span class="audit-time">[${now}]</span>
      <span class="audit-tag ${tagClass}">[${tag}]</span>
      <span class="audit-msg">${escapeHtml(msg)}</span>
    `;
    el.terminalLogs.appendChild(row);
    el.terminalLogs.scrollTop = el.terminalLogs.scrollHeight;
  }

  function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  // ==========================================================================
  // HARDWARE EVENT BINDINGS
  // ==========================================================================

  // Interactive Mascot Tap Reaction
  if (el.sproutMascot) {
    const cheerfulGiggles = [
      'Giggle! 🌱',
      'Growing strong! 🥦',
      'Wheee! ✨',
      'Loving the light! ☀️',
      'Happy sprout! 💖'
    ];
    let giggleIndex = 0;

    el.sproutMascot.addEventListener('click', () => {
      const svg = el.sproutMascot.querySelector('.brocco-svg');
      if (svg) {
        svg.classList.remove('brocco-hop');
        void svg.offsetWidth; // trigger reflow
        svg.classList.add('brocco-hop');
      }
      el.mascotMoodTag.textContent = cheerfulGiggles[giggleIndex % cheerfulGiggles.length];
      giggleIndex++;
      clearTimeout(mascotInteractionTimer);
      mascotInteractionTimer = setTimeout(() => {
        mascotInteractionTimer = null;
        if (telemetry) updateMascotEmotion(telemetry.pcs, telemetry);
      }, 3000);
    });
  }

  // Stepper Buttons (+ / -) for Setpoints
  document.querySelectorAll('.step-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const targetId = btn.dataset.target;
      const step = parseFloat(btn.dataset.step) || 1;
      const input = document.getElementById(targetId);
      if (!input) return;

      let val = parseFloat(input.value) || 0;
      if (btn.classList.contains('step-inc')) {
        val += step;
      } else {
        val -= step;
      }
      val = Math.round(val * 10) / 10;
      input.value = val;
      updateSetpointPreviews();
    });
  });

  // Numeric input change updates preview immediately
  [el.inputTempMin, el.inputTempMax, el.inputRhMin, el.inputRhMax, el.inputSoilMin, el.inputSoilMax].forEach(inp => {
    if (inp) inp.addEventListener('input', updateSetpointPreviews);
  });

  // Network Station Connect
  el.btnConnectTarget.addEventListener('click', () => {
    const target = setApiBase(el.targetHostInput.value);
    apiBase = target;
    appendAuditEntry(`Target host linked: ${apiBase}`, 'sys');
    fetchTelemetry();
  });

  el.targetHostInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') el.btnConnectTarget.click();
  });

  // Regulation Mode
  el.btnAutoMode.addEventListener('click', () => {
    const nextVal = (telemetry && telemetry.autoMode) ? 'manual' : 'auto';
    sendControl(`action=mode&val=${nextVal}`);
  });

  // Cultivation Phase
  el.btnPhase.addEventListener('click', () => {
    const nextVal = (telemetry && telemetry.phase === 1) ? 2 : 1;
    sendControl(`action=phase&val=${nextVal}`);
  });

  // Day Stepper
  el.btnDayDec.addEventListener('click', () => {
    const current = (telemetry && telemetry.day) ? telemetry.day : 1;
    const next = Math.max(1, current - 1);
    sendControl(`action=day&val=${next}`);
  });

  el.btnDayInc.addEventListener('click', () => {
    const current = (telemetry && telemetry.day) ? telemetry.day : 1;
    const next = Math.min(10, current + 1);
    sendControl(`action=day&val=${next}`);
  });

  // Fluidics Tank Switching
  el.btnToggleTank.addEventListener('click', () => {
    const next = (telemetry && telemetry.activeTank === 1) ? 2 : 1;
    sendControl(`action=tank&val=${next}`);
  });

  document.querySelectorAll('.res-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
      const tank = e.target.dataset.tank;
      sendControl(`action=refill&tank=${tank}`);
    });
  });

  // Emergency Purge
  el.btnEmergencyFlush.addEventListener('click', () => {
    sendControl('action=flush');
  });

  // 6 Power Relays
  for (let ch = 1; ch <= 6; ch++) {
    const btn = el.relays[ch].btn;
    if (btn) {
      btn.addEventListener('click', () => {
        sendControl(`action=toggleRelay&ch=${ch}`);
      });
    }
  }

  // Precision Servo Faders
  el.rangeValve1.addEventListener('input', (e) => {
    el.valValve1.textContent = `${e.target.value}°`;
  });
  el.rangeValve1.addEventListener('change', (e) => {
    sendControl(`action=setServo&servo=1&angle=${e.target.value}`);
  });

  el.rangeValve2.addEventListener('input', (e) => {
    el.valValve2.textContent = `${e.target.value}°`;
  });
  el.rangeValve2.addEventListener('change', (e) => {
    sendControl(`action=setServo&servo=2&angle=${e.target.value}`);
  });

  // Setpoint Synchronizer
  el.btnSyncThresholds.addEventListener('click', async () => {
    isSubmitting = true;
    const tMin = parseFloat(el.inputTempMin.value);
    const tMax = parseFloat(el.inputTempMax.value);
    const rMin = parseFloat(el.inputRhMin.value);
    const rMax = parseFloat(el.inputRhMax.value);
    const sMin = parseFloat(el.inputSoilMin.value);
    const sMax = parseFloat(el.inputSoilMax.value);

    await sendControl(`action=setThreshold&param=tempMin&val=${tMin}`);
    await sendControl(`action=setThreshold&param=tempMax&val=${tMax}`);
    await sendControl(`action=setThreshold&param=rhMin&val=${rMin}`);
    await sendControl(`action=setThreshold&param=rhMax&val=${rMax}`);
    await sendControl(`action=setThreshold&param=soilMin&val=${sMin}`);
    await sendControl(`action=setThreshold&param=soilMax&val=${sMax}`);

    appendAuditEntry('Setpoints committed to hardware controller', 'auto');
    hasUncommittedSetpoints = false;
    if (el.btnSyncThresholds) el.btnSyncThresholds.classList.remove('dirty');
    isSubmitting = false;
  });
  // Sensor Calibration
  el.btnCalibDry.addEventListener('click', () => {
    sendControl('action=calibrateSoil&type=dry');
  });

  el.btnCalibWet.addEventListener('click', () => {
    sendControl('action=calibrateSoil&type=wet');
  });

  // Clear Audit Log
  el.btnClearLogs.addEventListener('click', () => {
    el.terminalLogs.innerHTML = '';
  });

  // Start Poller
  fetchTelemetry();
  pollTimer = setInterval(fetchTelemetry, 1000);

})();
