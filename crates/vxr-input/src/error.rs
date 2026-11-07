use thiserror::Error;

#[derive(Error, Debug)]
pub enum InputError {
    #[error("ViGEmBus connection failed: {0}")]
    ViGEmConnect(String),

    #[error("ViGEmBus device error: {0}")]
    ViGEmDevice(String),

    #[error("Mapping error: {0}")]
    Mapping(String),

    #[error("Pose prediction error: {0}")]
    Prediction(String),

    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
}

pub type Result<T> = std::result::Result<T, InputError>;
