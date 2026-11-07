<script lang="ts">
  export let isStreaming = true;
  export let headsetName = "Meta Quest 3 (VR-Ready)";
  export let gpuName = "NVIDIA GeForce RTX 4060 Ti";
  export let activeRuntime = "VXR-XR Native OpenXR Runtime";
  export let onOpenPairing: () => void;
</script>

<header class="header">
  <div class="brand">
    <div class="logo">VXR</div>
    <div class="titles">
      <h1>PROJECT VXR</h1>
      <span class="subtitle">Wireless VR Streamer Host v1.0</span>
    </div>
  </div>

  <div class="status-chips">
    <div class="chip {isStreaming ? 'active' : 'inactive'}">
      <span class="dot"></span>
      {isStreaming ? 'STREAMING' : 'IDLE'}
    </div>
    <div class="chip info">
      <span class="icon">🥽</span>
      {headsetName}
    </div>
    <div class="chip info">
      <span class="icon">🎮</span>
      {gpuName}
    </div>
  </div>

  <div class="actions">
    <button class="btn-pair" on:click={onOpenPairing}>
      🔑 PAIR HEADSET
    </button>
  </div>
</header>

<style>
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 16px 24px;
    background: var(--bg-secondary);
    border-bottom: 1px solid var(--border);
  }
  .brand {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .logo {
    background: linear-gradient(135deg, var(--accent-cyan), var(--accent-purple));
    color: #000;
    font-weight: 900;
    font-size: 20px;
    padding: 6px 10px;
    border-radius: 8px;
    letter-spacing: 1px;
  }
  .titles h1 {
    font-size: 18px;
    font-weight: 700;
    letter-spacing: 0.5px;
  }
  .subtitle {
    font-size: 11px;
    color: var(--text-muted);
  }
  .status-chips {
    display: flex;
    align-items: center;
    gap: 10px;
  }
  .chip {
    display: flex;
    align-items: center;
    gap: 6px;
    background: var(--bg-card);
    border: 1px solid var(--border);
    padding: 6px 12px;
    border-radius: 20px;
    font-size: 12px;
    font-weight: 600;
  }
  .chip.active {
    border-color: var(--accent-green);
    color: var(--accent-green);
  }
  .chip.inactive {
    border-color: var(--accent-red);
    color: var(--accent-red);
  }
  .dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: currentColor;
    box-shadow: 0 0 8px currentColor;
  }
  .btn-pair {
    background: linear-gradient(135deg, #00e5ff, #7c4dff);
    color: #000;
    font-weight: 700;
    padding: 8px 16px;
    border-radius: 6px;
    font-size: 13px;
  }
  .btn-pair:hover {
    filter: brightness(1.15);
    transform: translateY(-1px);
  }
</style>
