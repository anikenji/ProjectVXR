use thiserror::Error;

#[derive(Error, Debug)]
pub enum PacerError {
    #[error("Queue capacity exceeded (current={current}, max={max})")]
    QueueFull { current: usize, max: usize },

    #[error("Socket error: {0}")]
    Socket(#[from] vxr_protocol::ProtocolError),

    #[error("Configuration error: {0}")]
    Config(String),

    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
}

pub type Result<T> = std::result::Result<T, PacerError>;
