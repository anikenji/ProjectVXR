<script lang="ts">
  export let metrics = {
    fps: 90.0,
    bitrate_mbps: 150.0,
    latency: {
      capture_ms: 1.2,
      encode_ms: 3.8,
      network_ms: 2.1,
      decode_ms: 5.4,
      total_motion_to_photon_ms: 12.5
    },
    packets_lost_pct: 0.0,
    fec_recovered_packets: 42,
    bufferbloat_queue_ms: 0.8
  };
</script>

<div class="dashboard">
  <div class="metrics-grid">
    <div class="card metric-card">
      <span class="label">FRAME RATE</span>
      <span class="value">{metrics.fps.toFixed(1)} <small>FPS</small></span>
      <span class="badge green">90Hz Stable</span>
    </div>

    <div class="card metric-card">
      <span class="label">STREAM BITRATE</span>
      <span class="value">{metrics.bitrate_mbps.toFixed(0)} <small>Mbps</small></span>
      <span class="badge cyan">Paced CBR</span>
    </div>

    <div class="card metric-card">
      <span class="label">TOTAL LATENCY</span>
      <span class="value highlight">{metrics.latency.total_motion_to_photon_ms.toFixed(1)} <small>ms</small></span>
      <span class="badge green">&lt; 15ms Ultra-Low</span>
    </div>

    <div class="card metric-card">
      <span class="label">NETWORK STABILITY</span>
      <span class="value">{metrics.packets_lost_pct.toFixed(2)}% <small>Loss</small></span>
      <span class="badge purple">{metrics.fec_recovered_packets} FEC Shards OK</span>
    </div>
  </div>

  <div class="card waterfall-card">
    <h3>MOTION-TO-PHOTON LATENCY WATERFALL</h3>
    <p class="subtitle">End-to-end breakdown from DXGI VRAM capture to Quest 3 display photons</p>

    <div class="waterfall-bars">
      <div class="bar-group">
        <div class="bar-label">
          <span>1. Desktop Capture (DXGI VRAM)</span>
          <strong>{metrics.latency.capture_ms} ms</strong>
        </div>
        <div class="bar-track">
          <div class="bar-fill capture" style="width: {(metrics.latency.capture_ms / 25) * 100}%"></div>
        </div>
      </div>

      <div class="bar-group">
        <div class="bar-label">
          <span>2. NVENC Hardware Encoding</span>
          <strong>{metrics.latency.encode_ms} ms</strong>
        </div>
        <div class="bar-track">
          <div class="bar-fill encode" style="width: {(metrics.latency.encode_ms / 25) * 100}%"></div>
        </div>
      </div>

      <div class="bar-group">
        <div class="bar-label">
          <span>3. Wi-Fi 6E Paced Network (40-60µs)</span>
          <strong>{metrics.latency.network_ms} ms</strong>
        </div>
        <div class="bar-track">
          <div class="bar-fill network" style="width: {(metrics.latency.network_ms / 25) * 100}%"></div>
        </div>
      </div>

      <div class="bar-group">
        <div class="bar-label">
          <span>4. AMediaCodec Decode + Vulkan Zero-Copy</span>
          <strong>{metrics.latency.decode_ms} ms</strong>
        </div>
        <div class="bar-track">
          <div class="bar-fill decode" style="width: {(metrics.latency.decode_ms / 25) * 100}%"></div>
        </div>
      </div>
    </div>

    <div class="waterfall-total">
      <span>Total End-to-End Latency:</span>
      <span class="total-badge">{metrics.latency.total_motion_to_photon_ms} ms</span>
    </div>
  </div>
</div>

<style>
  .dashboard {
    display: flex;
    flex-direction: column;
    gap: 20px;
  }
  .metrics-grid {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 16px;
  }
  .card {
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 16px 20px;
  }
  .metric-card {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  .label {
    font-size: 11px;
    color: var(--text-muted);
    font-weight: 700;
    letter-spacing: 0.5px;
  }
  .value {
    font-size: 26px;
    font-weight: 800;
  }
  .value small {
    font-size: 14px;
    font-weight: 500;
    color: var(--text-muted);
  }
  .value.highlight {
    color: var(--accent-cyan);
  }
  .badge {
    align-self: flex-start;
    padding: 2px 8px;
    border-radius: 12px;
    font-size: 10px;
    font-weight: 700;
  }
  .badge.green { background: rgba(0, 230, 118, 0.15); color: var(--accent-green); }
  .badge.cyan { background: rgba(0, 229, 255, 0.15); color: var(--accent-cyan); }
  .badge.purple { background: rgba(124, 77, 255, 0.15); color: var(--accent-purple); }

  .waterfall-card h3 {
    font-size: 15px;
    margin-bottom: 4px;
    letter-spacing: 0.5px;
  }
  .subtitle {
    font-size: 12px;
    color: var(--text-muted);
    margin-bottom: 16px;
  }
  .waterfall-bars {
    display: flex;
    flex-direction: column;
    gap: 14px;
  }
  .bar-label {
    display: flex;
    justify-content: space-between;
    font-size: 13px;
    margin-bottom: 6px;
  }
  .bar-track {
    width: 100%;
    height: 10px;
    background: var(--bg-secondary);
    border-radius: 6px;
    overflow: hidden;
  }
  .bar-fill {
    height: 100%;
    border-radius: 6px;
    transition: width 0.3s ease;
  }
  .bar-fill.capture { background: #ffab00; }
  .bar-fill.encode  { background: #7c4dff; }
  .bar-fill.network { background: #00e5ff; }
  .bar-fill.decode  { background: #00e676; }

  .waterfall-total {
    margin-top: 18px;
    padding-top: 14px;
    border-top: 1px solid var(--border);
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 14px;
    font-weight: 600;
  }
  .total-badge {
    background: linear-gradient(135deg, var(--accent-cyan), var(--accent-purple));
    color: #000;
    font-weight: 800;
    padding: 4px 12px;
    border-radius: 6px;
  }
</style>
