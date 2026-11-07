<script lang="ts">
  import Header from "./components/Header.svelte";
  import MetricsDashboard from "./components/MetricsDashboard.svelte";
  import SettingsPanel from "./components/SettingsPanel.svelte";
  import MonitorManager from "./components/MonitorManager.svelte";
  import PairingModal from "./components/PairingModal.svelte";

  let activeTab: "metrics" | "settings" | "monitors" = "metrics";
  let isPairingOpen = false;
  let currentPin = "849201";

  let systemStatus = {
    is_streaming: true,
    connected_headset: "Meta Quest 3 (VR-Ready)",
    gpu_name: "NVIDIA GeForce RTX 4060 Ti",
    active_runtime: "VXR-XR Native OpenXR Runtime",
    network_transport: "Wi-Fi 6E (160MHz P2P Direct)"
  };

  let metrics = {
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

  let settings = {
    codec: "HEVC 10-bit",
    target_bitrate_mbps: 150,
    target_fps: 90,
    enable_two_pass: true,
    enable_spatial_aq: true,
    enable_intra_refresh: true,
    virtual_monitors_count: 1,
    virtual_monitors_resolution: "2064x2208"
  };

  let pairedDevices = [
    "Meta Quest 3 (SN: 1WMHH81001) - 5GHz P2P Direct"
  ];

  function handleSaveSettings(newSettings: typeof settings) {
    settings = { ...newSettings };
    metrics.bitrate_mbps = newSettings.target_bitrate_mbps;
    metrics.fps = newSettings.target_fps;
    alert("Streaming configuration saved and applied to hardware encoder!");
  }

  function handleApplyMonitors(count: number, res: string) {
    settings.virtual_monitors_count = count;
    settings.virtual_monitors_resolution = res;
    alert(`IDDCX Virtual Display Driver reconfigured: ${count} display(s) at ${res} @ 120Hz`);
  }

  function handleGeneratePin() {
    currentPin = Math.floor(100000 + Math.random() * 900000).toString();
  }
</script>

<main class="app-container">
  <Header
    isStreaming={systemStatus.is_streaming}
    headsetName={systemStatus.connected_headset}
    gpuName={systemStatus.gpu_name}
    activeRuntime={systemStatus.active_runtime}
    onOpenPairing={() => isPairingOpen = true}
  />

  <nav class="nav-bar">
    <button
      class="nav-tab {activeTab === 'metrics' ? 'active' : ''}"
      on:click={() => activeTab = 'metrics'}
    >
      📊 LIVE MONITORING &amp; LATENCY
    </button>
    <button
      class="nav-tab {activeTab === 'settings' ? 'active' : ''}"
      on:click={() => activeTab = 'settings'}
    >
      ⚙️ ENCODER &amp; STREAM SETTINGS
    </button>
    <button
      class="nav-tab {activeTab === 'monitors' ? 'active' : ''}"
      on:click={() => activeTab = 'monitors'}
    >
      🖥️ VIRTUAL MONITORS (IDDCX)
    </button>
  </nav>

  <section class="content-body">
    {#if activeTab === 'metrics'}
      <MetricsDashboard {metrics} />
    {:else if activeTab === 'settings'}
      <SettingsPanel {settings} onSave={handleSaveSettings} />
    {:else if activeTab === 'monitors'}
      <MonitorManager
        monitorCount={settings.virtual_monitors_count}
        monitorResolution={settings.virtual_monitors_resolution}
        onApplyMonitors={handleApplyMonitors}
      />
    {/if}
  </section>

  <PairingModal
    isOpen={isPairingOpen}
    pairingPin={currentPin}
    {pairedDevices}
    onClose={() => isPairingOpen = false}
    onGeneratePin={handleGeneratePin}
  />
</main>

<style>
  .app-container {
    display: flex;
    flex-direction: column;
    min-height: 100vh;
  }
  .nav-bar {
    display: flex;
    gap: 8px;
    padding: 12px 24px 0 24px;
    background: var(--bg-secondary);
  }
  .nav-tab {
    background: none;
    color: var(--text-muted);
    font-size: 13px;
    font-weight: 700;
    padding: 10px 16px;
    border-bottom: 2px solid transparent;
  }
  .nav-tab:hover { color: var(--text-main); }
  .nav-tab.active {
    color: var(--accent-cyan);
    border-bottom-color: var(--accent-cyan);
  }
  .content-body {
    padding: 24px;
    flex: 1;
  }
</style>
