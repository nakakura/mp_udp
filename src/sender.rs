#![feature(drain_filter)]
extern crate futures;
extern crate tokio_core;
extern crate tokio_io;
extern crate bincode;
extern crate rustc_serialize;

mod udp;

use tokio_core::reactor::Core;
use futures::*;
use futures::sync::mpsc;

use std::thread;
use std::net::SocketAddr;

fn main() {
    let target_addr_str = "127.0.0.1";
    let target_addr_1: SocketAddr = format!("{}:{}", target_addr_str, 10001).parse().unwrap();
    let target_addr_2: SocketAddr = format!("{}:{}", target_addr_str, 10002).parse().unwrap();
    let target_addr_3: SocketAddr = format!("{}:{}", target_addr_str, 10003).parse().unwrap();

    let (multi_path_udp_1st_tx, multi_path_udp_1st_rx) = mpsc::channel::<Vec<u8>>(5000);
    let (multi_path_udp_2nd_tx, multi_path_udp_2nd_rx) = mpsc::channel::<Vec<u8>>(5000);
    let (multi_path_udp_3rd_tx, multi_path_udp_3rd_rx) = mpsc::channel::<Vec<u8>>(5000);

    let th_send_1 = udp::sender(multi_path_udp_1st_rx.map(move |x| (target_addr_1, x)));
    let th_send_2 = udp::sender(multi_path_udp_2nd_rx.map(move |x| (target_addr_2, x)));
    let th_send_3 = udp::sender(multi_path_udp_3rd_rx.map(move |x| (target_addr_3, x)));

    let (recv_udp_tx, recv_udp_rx) = mpsc::channel::<Vec<u8>>(5000);
    let recv_addr: SocketAddr = format!("0.0.0.0:{}", 10000).parse().unwrap();
    let th_recv = udp::receiver(recv_addr, recv_udp_tx);

    let th_broadcast = thread::spawn(|| {
        let mut core = Core::new().unwrap();
        let r = recv_udp_rx.fold((multi_path_udp_1st_tx, multi_path_udp_2nd_tx, multi_path_udp_3rd_tx), broadcast);
        let _ = core.run(r);
    });

    let _ = th_recv.join();
    let _ = th_send_1.join();
    let _ = th_send_2.join();
    let _ = th_send_3.join();
    let _ = th_broadcast;
}

type SenderTuple = (mpsc::Sender<Vec<u8>>, mpsc::Sender<Vec<u8>>, mpsc::Sender<Vec<u8>>);

fn broadcast(sum: SenderTuple, acc: Vec<u8>) -> Result<SenderTuple, ()> {
    let s1 = sum.0.send(acc.clone()).wait().unwrap();
    let s2 = sum.1.send(acc.clone()).wait().unwrap();
    let s3 = sum.2.send(acc).wait().unwrap();
    Ok((s1, s2, s3))
}

