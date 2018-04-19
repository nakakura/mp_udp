use std::io;
use std::net::SocketAddr;
use std::thread;

use tokio_core::net::UdpSocket;
use tokio_core::reactor::Core;
use tokio_core::net::UdpCodec;
use futures::{Future, Stream};
use futures::sync::mpsc;
use futures::Sink;

pub struct LineCodec;

impl UdpCodec for LineCodec {
    type In = (SocketAddr, Vec<u8>);
    type Out = (SocketAddr, Vec<u8>);

    fn decode(&mut self, addr: &SocketAddr, buf: &[u8]) -> io::Result<Self::In> {
        Ok((*addr, buf.to_vec()))
    }

    fn encode(&mut self, (addr, buf): Self::Out, into: &mut Vec<u8>) -> SocketAddr {
        into.extend(buf);
        addr
    }
}

pub fn receiver(addr: SocketAddr, tx: mpsc::Sender<Vec<u8>>) -> thread::JoinHandle<()> {
    thread::spawn(move || {
        let mut core = Core::new().unwrap();
        let handle = core.handle();

        let a = UdpSocket::bind(&addr, &handle).unwrap();

        let (_, a_stream) = a.framed(LineCodec).split();
        let a = a_stream.map_err(|_| ()).fold(tx, |sender, (_, x): (SocketAddr, Vec<u8>)| {
            let sender = sender.send(x).wait().unwrap();
            Ok(sender)
        });
        drop(core.run(a));
    })
}

pub fn sender<S>(stream: S) -> thread::JoinHandle<()>
    where S: Stream<Item=(SocketAddr, Vec<u8>), Error=()> + Send + Sized + 'static {
    thread::spawn(move || {
        let mut core = Core::new().unwrap();
        let handle = core.handle();

        let local_addr: SocketAddr = "0.0.0.0:0".parse().unwrap();

        let a = UdpSocket::bind(&local_addr, &handle).unwrap();
        let (a_sink, _) = a.framed(LineCodec).split();

        let sender = a_sink.sink_map_err(|e| {
            eprintln!("err {:?}", e);
        }).send_all(stream);
        //handle.spawn(sender.then(|_| Ok(())));
        drop(core.run(sender));
    })
}
