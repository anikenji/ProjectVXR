<script lang="ts">
  export let settings = {
    codec: "HEVC 10-bit",
    target_bitrate_mbps: 150,
    target_fps: 90,
    enable_two_pass: true,
    enable_spatial_aq: true,
    enable_intra_refresh: true,
    virtual_monitors_count: 1,
    virtual_monitors_resolution: "2064x2208"
  };

  export let onSave: (newSettings: typeof settings) => void;
</script>

<div class="card settings-card">
  <h3>STREAMING & ENCODING SETTINGS</h3>
  <p class="subtitle">Fine-tune hardware video encoding, compression quality, and network pacing</p>

  <div class="form-grid">
    <div class="form-group">
      <label for="codec">Video Codec</label>
      <select id="codec" bind:value={settings.codec}>
        <option value="HEVC 10-bit">HEVC 10-bit (P010) - Recommended for Quest 3</option>
        <option value="AV1 10-bit">AV1 10-bit (RTX 40-Series Ada Lovelace)</option>
        <option value="H.264+">H.264+ High Bitrate (Max Compatibility)</option>
      </select>
      <span class="hint">10-bit color eliminates dark banding in VR fog and night scenes.</span>
    </div>

    <div class="form-group">
      <label for="fps">Target Refresh Rate</label>
      <div class="btn-group">
        {#each [72, 90, 120] as rate}
          <button
            class="btn-toggle {settings.target_fps === rate ? 'selected' : ''}"
            on:click={() => settings.target_fps = rate}
          >
            {rate} Hz
          </button>
        {/each}
      </div>
    </div>

    <div class="form-group full-width">
      <div class="slider-header">
        <label for="bitrate">Target Bitrate</label>
        <strong class="bitrate-val">{settings.target_bitrate_mbps} Mbps</strong>
      </div>
      <input
        id="bitrate"
        type="range"
        min="30"
        max="450"
        step="10"
        bind:value={settings.target_bitrate_mbps}
      />
      <div class="slider-ticks">
        <span>30 Mbps</span>
        <span>150 Mbps (Wi-Fi 6)</span>
        <span>400+ Mbps (Wi-Fi 6E)</span>
      </div>
    </div>

    <div class="form-group toggles full-width">
      <label class="toggle-item">
        <input type="checkbox" bind:checked={settings.enable_two_pass} />
        <div>
          <strong>2-Pass Encoding</strong>
          <p>Enables quarter-resolution first-pass analysis for sharp text boundaries.</p>
        </div>
      </label>

      <label class="toggle-item">
        <input type="checkbox" bind:checked={settings.enable_spatial_aq} />
        <div>
          <strong>Adaptive Quantization (AQ)</strong>
          <p>Allocates bits intelligently to high-motion and eye focus areas.</p>
        </div>
      </label>

      <label class="toggle-item">
        <input type="checkbox" bind:checked={settings.enable_intra_refresh} />
        <div>
          <strong>Periodic Intra-Refresh (Sliced)</strong>
          <p>Rolls intra-macroblocks across 8 slices to eliminate IDR packet spikes.</p>
        </div>
      </label>
    </div>
  </div>

  <div class="save-bar">
    <button class="btn-save" on:click={() => onSave(settings)}>
      APPLY SETTINGS
    </button>
  </div>
</div>

<style>
  .settings-card {
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 20px 24px;
  }
  h3 { font-size: 15px; margin-bottom: 4px; }
  .subtitle { font-size: 12px; color: var(--text-muted); margin-bottom: 20px; }
  .form-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 20px;
  }
  .full-width { grid-column: span 2; }
  .form-group {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  label { font-size: 12px; font-weight: 700; color: var(--text-muted); }
  .hint { font-size: 11px; color: var(--text-muted); }
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
  .slider-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }
  .bitrate-val { color: var(--accent-cyan); font-size: 16px; }
  .slider-ticks {
    display: flex;
    justify-content: space-between;
    font-size: 10px;
    color: var(--text-muted);
  }
  .toggles {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 16px;
    margin-top: 10px;
  }
  .toggle-item {
    display: flex;
    gap: 10px;
    align-items: flex-start;
    background: var(--bg-secondary);
    padding: 12px;
    border-radius: 6px;
    border: 1px solid var(--border);
    cursor: pointer;
  }
  .toggle-item strong { display: block; font-size: 12px; color: var(--text-main); margin-bottom: 4px; }
  .toggle-item p { font-size: 11px; color: var(--text-muted); line-height: 1.3; }
  .save-bar {
    margin-top: 24px;
    display: flex;
    justify-content: flex-end;
  }
  .btn-save {
    background: var(--accent-cyan);
    color: #000;
    font-weight: 800;
    padding: 10px 24px;
    border-radius: 6px;
    font-size: 13px;
  }
  .btn-save:hover { filter: brightness(1.2); }
</style>
