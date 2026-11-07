<script lang="ts">
  export let monitorCount = 1;
  export let monitorResolution = "2064x2208";
  export let onApplyMonitors: (count: number, res: string) => void;
</script>

<div class="card monitor-card">
  <h3>VIRTUAL DISPLAY DRIVER (IDDCX)</h3>
  <p class="subtitle">Create and manage headless virtual displays without physical monitors or HDMI plugs</p>

  <div class="monitor-controls">
    <div class="control-group">
      <label>Number of Virtual Displays</label>
      <div class="btn-group">
        {#each [1, 2, 3] as count}
          <button
            class="btn-toggle {monitorCount === count ? 'selected' : ''}"
            on:click={() => monitorCount = count}
          >
            {count} {count === 1 ? 'Monitor' : 'Monitors'}
          </button>
        {/each}
      </div>
    </div>

    <div class="control-group">
      <label>Virtual Monitor Resolution</label>
      <select bind:value={monitorResolution}>
        <option value="2064x2208">2064x2208 (Native Meta Quest 3 Panel)</option>
        <option value="1920x1080">1920x1080 (1080p Full HD)</option>
        <option value="2560x1440">2560x1440 (1440p Quad HD)</option>
        <option value="3840x2160">3840x2160 (4K Ultra HD)</option>
      </select>
    </div>
  </div>

  <div class="monitor-preview">
    {#each Array(monitorCount) as _, i}
      <div class="monitor-box">
        <div class="screen-frame">
          <div class="screen-content">
            <span class="screen-badge">DISPLAY #{i + 1}</span>
            <strong>{monitorResolution}</strong>
            <small>EDID: VXR-VDA-0{i + 1}</small>
          </div>
        </div>
        <div class="monitor-stand"></div>
      </div>
    {/each}
  </div>

  <div class="action-bar">
    <button class="btn-apply" on:click={() => onApplyMonitors(monitorCount, monitorResolution)}>
      RECONFIGURE DISPLAYS
    </button>
  </div>
</div>

<style>
  .monitor-card {
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 20px 24px;
  }
  h3 { font-size: 15px; margin-bottom: 4px; }
  .subtitle { font-size: 12px; color: var(--text-muted); margin-bottom: 20px; }
  .monitor-controls {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 20px;
    margin-bottom: 24px;
  }
  .control-group {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  label { font-size: 12px; font-weight: 700; color: var(--text-muted); }
  .btn-group { display: flex; gap: 8px; }
  .btn-toggle {
    flex: 1;
    padding: 8px;
    background: var(--bg-secondary);
    border: 1px solid var(--border);
    color: var(--text-main);
    border-radius: 6px;
    font-weight: 600;
  }
  .btn-toggle.selected {
    background: var(--accent-cyan);
    color: #000;
    border-color: var(--accent-cyan);
  }

  .monitor-preview {
    display: flex;
    justify-content: center;
    gap: 24px;
    padding: 30px;
    background: var(--bg-secondary);
    border-radius: 8px;
    border: 1px dashed var(--border);
  }
  .monitor-box {
    display: flex;
    flex-direction: column;
    align-items: center;
    width: 200px;
  }
  .screen-frame {
    width: 100%;
    height: 120px;
    background: #000;
    border: 2px solid var(--accent-cyan);
    border-radius: 6px;
    padding: 10px;
    display: flex;
    align-items: center;
    justify-content: center;
    box-shadow: 0 0 15px rgba(0, 229, 255, 0.2);
  }
  .screen-content {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 4px;
  }
  .screen-badge {
    background: var(--accent-cyan);
    color: #000;
    font-size: 9px;
    font-weight: 800;
    padding: 2px 6px;
    border-radius: 4px;
  }
  .screen-content strong { font-size: 13px; }
  .screen-content small { font-size: 10px; color: var(--text-muted); }
  .monitor-stand {
    width: 30px;
    height: 16px;
    background: var(--border);
    margin-top: 2px;
  }
  .action-bar {
    margin-top: 20px;
    display: flex;
    justify-content: flex-end;
  }
  .btn-apply {
    background: var(--accent-cyan);
    color: #000;
    font-weight: 800;
    padding: 10px 24px;
    border-radius: 6px;
    font-size: 13px;
  }
  .btn-apply:hover { filter: brightness(1.2); }
</style>
