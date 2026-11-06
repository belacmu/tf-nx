// Browser (Emscripten) stand-in for relay.rs. TF.EX's relay carries game packets over UDP, which a page can't
// open, and TF.EX v0.21 hands every session a relay URL (NetplayManager.ApplyRelay), so the routed transport
// opens one. This relay never connects and never reaches a peer, so every peer stays on the direct path
// (WebRTC, or the site's own WebSocket relay, see netplay/tfnet.js), and it never ends the session.
use futures::FutureExt;
use ggrs::{Message, NonBlockingSocket, PlayerType};
use matchbox_socket::PeerId;
use tracing::info;

use crate::{config::ggrs_config::Address, transport::Connection};

pub struct RelaySocket;

#[derive(Clone)]
pub struct RelayStatus;

impl RelayStatus {
    pub fn is_refused(&self) -> bool {
        false
    }
}

pub struct RelayChannel;

pub fn connect(connection_url: String) -> Result<(RelaySocket, RelayChannel, Connection), String> {
    info!("Relay not available in the browser, ignoring {connection_url}");

    Ok((RelaySocket, RelayChannel, futures::future::pending().boxed()))
}

impl RelaySocket {
    pub fn update_peers(&mut self) {}

    pub fn connected_peers(&self) -> Vec<PeerId> {
        vec![]
    }

    pub fn players(&self) -> Vec<PlayerType<PeerId>> {
        vec![PlayerType::Local]
    }

    pub fn status(&self) -> RelayStatus {
        RelayStatus
    }

    pub fn release(&mut self) {}
}

impl RelayChannel {
    pub fn send_packet(&mut self, _packet: &[u8], _peer: PeerId) -> bool {
        false
    }

    pub fn receive_packets(&mut self) -> Vec<(PeerId, Box<[u8]>)> {
        vec![]
    }
}

impl NonBlockingSocket<Address> for RelayChannel {
    fn send_to(&mut self, _msg: &Message, _addr: &Address) {}

    fn receive_all_messages(&mut self) -> Vec<(Address, Message)> {
        vec![]
    }
}
