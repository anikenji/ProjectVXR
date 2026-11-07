<script lang="ts">
  export let isOpen = false;
  export let pairingPin = "849201";
  export let pairedDevices: string[] = [];
  export let onClose: () => void;
  export let onGeneratePin: () => void;
</script>

{#if isOpen}
  <div class="backdrop" on:click={onClose}>
    <div class="modal" on:click|stopPropagation>
      <div class="modal-header">
        <h2>PAIR META QUEST HEADSET</h2>
        <button class="btn-close" on:click={onClose}>✕</button>
      </div>

      <div class="modal-body">
        <p class="desc">
          Enter this 6-digit PIN code on your Meta Quest headset to securely pair via local HMAC-SHA256 authentication:
        </p>

        <div class="pin-display">
          {#each pairingPin.split('') as digit}
            <span class="pin-box">{digit}</span>
          {/each}
        </div>

        <div class="pin-actions">
          <button class="btn-refresh" on:click={onGeneratePin}>
            🔄 GENERATE NEW PIN
          </button>
        </div>

        <div class="devices-section">
          <h4>TRUSTED PAIRED HEADSETS</h4>
          {#if pairedDevices.length === 0}
            <p class="empty">No headsets currently paired.</p>
          {:else}
            <div class="device-list">
              {#each pairedDevices as dev}
                <div class="device-item">
                  <span class="dev-icon">🥽</span>
                  <span class="dev-name">{dev}</span>
                  <span class="dev-status">TRUSTED</span>
                </div>
              {/each}
            </div>
          {/if}
        </div>
      </div>
    </div>
  </div>
{/if}

<style>
  .backdrop {
    position: fixed;
    top: 0; left: 0; right: 0; bottom: 0;
    background: rgba(0, 0, 0, 0.75);
    backdrop-filter: blur(4px);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 999;
  }
  .modal {
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 12px;
    width: 480px;
    box-shadow: 0 10px 40px rgba(0, 0, 0, 0.8);
    overflow: hidden;
  }
  .modal-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 16px 20px;
    border-bottom: 1px solid var(--border);
  }
  .modal-header h2 { font-size: 15px; font-weight: 700; letter-spacing: 0.5px; }
  .btn-close {
    background: none;
    color: var(--text-muted);
    font-size: 16px;
  }
  .btn-close:hover { color: #fff; }
  .modal-body {
    padding: 24px;
    display: flex;
    flex-direction: column;
    align-items: center;
  }
  .desc {
    font-size: 13px;
    color: var(--text-muted);
    text-align: center;
    line-height: 1.4;
    margin-bottom: 20px;
  }
  .pin-display {
    display: flex;
    gap: 12px;
    margin-bottom: 16px;
  }
  .pin-box {
    width: 48px;
    height: 60px;
    background: var(--bg-secondary);
    border: 2px solid var(--accent-cyan);
    border-radius: 8px;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 28px;
    font-weight: 800;
    color: var(--accent-cyan);
    box-shadow: 0 0 15px rgba(0, 229, 255, 0.25);
  }
  .btn-refresh {
    background: var(--bg-secondary);
    border: 1px solid var(--border);
    color: var(--text-muted);
    padding: 6px 14px;
    border-radius: 4px;
    font-size: 11px;
    font-weight: 700;
    margin-bottom: 24px;
  }
  .btn-refresh:hover { color: var(--text-main); border-color: var(--accent-cyan); }
  .devices-section {
    width: 100%;
    border-top: 1px solid var(--border);
    padding-top: 16px;
  }
  .devices-section h4 {
    font-size: 12px;
    color: var(--text-muted);
    margin-bottom: 10px;
    letter-spacing: 0.5px;
  }
  .empty { font-size: 12px; color: var(--text-muted); font-style: italic; }
  .device-list {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  .device-item {
    display: flex;
    align-items: center;
    justify-content: space-between;
    background: var(--bg-secondary);
    padding: 8px 12px;
    border-radius: 6px;
    font-size: 12px;
  }
  .dev-name { font-weight: 600; flex: 1; margin-left: 8px; }
  .dev-status {
    font-size: 10px;
    font-weight: 700;
    color: var(--accent-green);
    background: rgba(0, 230, 118, 0.1);
    padding: 2px 6px;
    border-radius: 4px;
  }
</style>
