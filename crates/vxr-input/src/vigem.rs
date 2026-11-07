use crate::error::{InputError, Result};
use crate::mapping::{buttons, VirtualGamepadReport};
use std::sync::Arc;
use vigem_client::{Client, TargetId, XButtons, XGamepad, Xbox360Wired};

pub trait GamepadDevice: Send {
    fn update(&mut self, report: &VirtualGamepadReport) -> Result<()>;
    fn disconnect(&mut self) -> Result<()>;
    fn is_real_hardware(&self) -> bool;
    fn last_report(&self) -> VirtualGamepadReport;
}

/// Real Xbox 360 virtual gamepad backed by the ViGEmBus kernel driver
pub struct ViGEmXbox360 {
    target: Xbox360Wired<Arc<Client>>,
    last_report: VirtualGamepadReport,
}

impl ViGEmXbox360 {
    pub fn new(client: Arc<Client>) -> Result<Self> {
        let mut target = Xbox360Wired::new(client, TargetId::XBOX360_WIRED);
        target.plugin().map_err(|e| InputError::ViGEmDevice(format!("Failed to plugin Xbox360: {:?}", e)))?;
        let _ = target.wait_ready();

        Ok(Self {
            target,
            last_report: VirtualGamepadReport::default(),
        })
    }

    pub fn to_xgamepad(report: &VirtualGamepadReport) -> XGamepad {
        let mut xbtn: u16 = 0;
        if (report.buttons & buttons::DPAD_UP) != 0 { xbtn |= XButtons::UP; }
        if (report.buttons & buttons::DPAD_DOWN) != 0 { xbtn |= XButtons::DOWN; }
        if (report.buttons & buttons::DPAD_LEFT) != 0 { xbtn |= XButtons::LEFT; }
        if (report.buttons & buttons::DPAD_RIGHT) != 0 { xbtn |= XButtons::RIGHT; }
        if (report.buttons & buttons::START) != 0 { xbtn |= XButtons::START; }
        if (report.buttons & buttons::BACK) != 0 { xbtn |= XButtons::BACK; }
        if (report.buttons & buttons::LEFT_THUMB) != 0 { xbtn |= XButtons::LTHUMB; }
        if (report.buttons & buttons::RIGHT_THUMB) != 0 { xbtn |= XButtons::RTHUMB; }
        if (report.buttons & buttons::LEFT_SHOULDER) != 0 { xbtn |= XButtons::LB; }
        if (report.buttons & buttons::RIGHT_SHOULDER) != 0 { xbtn |= XButtons::RB; }
        if (report.buttons & buttons::GUIDE) != 0 { xbtn |= XButtons::GUIDE; }
        if (report.buttons & buttons::A) != 0 { xbtn |= XButtons::A; }
        if (report.buttons & buttons::B) != 0 { xbtn |= XButtons::B; }
        if (report.buttons & buttons::X) != 0 { xbtn |= XButtons::X; }
        if (report.buttons & buttons::Y) != 0 { xbtn |= XButtons::Y; }

        let mut xg = XGamepad {
            thumb_lx: report.thumb_lx,
            thumb_ly: report.thumb_ly,
            thumb_rx: report.thumb_rx,
            thumb_ry: report.thumb_ry,
            left_trigger: report.left_trigger,
            right_trigger: report.right_trigger,
            ..Default::default()
        };
        xg.buttons.raw = xbtn;
        xg
    }
}

impl GamepadDevice for ViGEmXbox360 {
    fn update(&mut self, report: &VirtualGamepadReport) -> Result<()> {
        let xg = Self::to_xgamepad(report);
        self.target.update(&xg)
            .map_err(|e| InputError::ViGEmDevice(format!("Xbox360 update failed: {:?}", e)))?;
        self.last_report = *report;
        Ok(())
    }

    fn disconnect(&mut self) -> Result<()> {
        let _ = self.target.unplug();
        Ok(())
    }

    fn is_real_hardware(&self) -> bool {
        true
    }

    fn last_report(&self) -> VirtualGamepadReport {
        self.last_report
    }
}

/// Mock virtual gamepad used in environments where ViGEmBus kernel driver is not active (CI/headless)
pub struct MockGamepadDevice {
    name: String,
    last_report: VirtualGamepadReport,
    update_count: u64,
}

impl MockGamepadDevice {
    pub fn new(name: &str) -> Self {
        Self {
            name: name.to_string(),
            last_report: VirtualGamepadReport::default(),
            update_count: 0,
        }
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn update_count(&self) -> u64 {
        self.update_count
    }
}

impl GamepadDevice for MockGamepadDevice {
    fn update(&mut self, report: &VirtualGamepadReport) -> Result<()> {
        self.last_report = *report;
        self.update_count += 1;
        Ok(())
    }

    fn disconnect(&mut self) -> Result<()> {
        Ok(())
    }

    fn is_real_hardware(&self) -> bool {
        false
    }

    fn last_report(&self) -> VirtualGamepadReport {
        self.last_report
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GamepadKind {
    Xbox360,
    DualShock4,
}

/// ViGEm Emulator Manager
pub struct ViGEmManager {
    client: Option<Arc<Client>>,
}

impl Default for ViGEmManager {
    fn default() -> Self {
        Self::new()
    }
}

impl ViGEmManager {
    pub fn new() -> Self {
        let client = Client::connect().ok().map(Arc::new);
        if client.is_some() {
            log::info!("[VXR-Input] Successfully connected to ViGEmBus kernel driver.");
        } else {
            log::warn!("[VXR-Input] ViGEmBus driver not active; using mock virtual gamepad fallback.");
        }
        Self { client }
    }

    pub fn is_bus_available(&self) -> bool {
        self.client.is_some()
    }

    pub fn create_gamepad(&self, kind: GamepadKind) -> Box<dyn GamepadDevice> {
        if let Some(ref client) = self.client {
            match kind {
                GamepadKind::Xbox360 => {
                    if let Ok(dev) = ViGEmXbox360::new(client.clone()) {
                        return Box::new(dev);
                    }
                }
                GamepadKind::DualShock4 => {
                    // For DS4 fallback/mock
                    return Box::new(MockGamepadDevice::new("DualShock 4 Virtual Controller"));
                }
            }
        }

        let name = match kind {
            GamepadKind::Xbox360 => "Xbox 360 Virtual Controller (Mock)",
            GamepadKind::DualShock4 => "DualShock 4 Virtual Controller (Mock)",
        };
        Box::new(MockGamepadDevice::new(name))
    }
}
