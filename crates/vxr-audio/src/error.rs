use thiserror::Error;

#[derive(Error, Debug)]
pub enum AudioError {
    #[error("Opus codec error: {0}")]
    Opus(String),

    #[error("WASAPI error: {0}")]
    Wasapi(String),

    #[error("Buffer error: {0}")]
    Buffer(String),

    #[error("Invalid audio format: sample_rate={sample_rate}, channels={channels}")]
    InvalidFormat { sample_rate: u32, channels: u16 },

    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
}

pub type Result<T> = std::result::Result<T, AudioError>;
