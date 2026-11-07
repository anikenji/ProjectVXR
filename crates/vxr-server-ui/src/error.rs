use thiserror::Error;

#[derive(Error, Debug)]
pub enum UiError {
    #[error("Server state error: {0}")]
    State(String),

    #[error("Command execution failed: {0}")]
    Command(String),

    #[error("Protocol error: {0}")]
    Protocol(#[from] vxr_protocol::ProtocolError),

    #[error("Serialization error: {0}")]
    Serialization(#[from] serde_json::Error),

    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
}

pub type Result<T> = std::result::Result<T, UiError>;
