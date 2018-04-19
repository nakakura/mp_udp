#![feature(drain_filter)]
extern crate futures;
extern crate tokio_core;
extern crate tokio_io;
extern crate bincode;
extern crate rustc_serialize;
#[macro_use] extern crate lazy_static;

mod udp;
mod rtp;

use tokio_core::reactor::Core;
use futures::*;
use futures::sync::mpsc;

use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::RwLock;

lazy_static! {
    pub static ref HASHMAP: RwLock<HashMap<u32, bool>> = {
        RwLock::new(HashMap::new())
    };
}

fn main() {
    let recv_addr_str = "0.0.0.0";
    let recv_addr_1: SocketAddr = format!("{}:{}", recv_addr_str, 10001).parse().unwrap();
    let recv_addr_2: SocketAddr = format!("{}:{}", recv_addr_str, 10002).parse().unwrap();
    let recv_addr_3: SocketAddr = format!("{}:{}", recv_addr_str, 10003).parse().unwrap();

    let (multi_path_udp_1st_tx, multi_path_udp_1st_rx) = mpsc::channel::<Vec<u8>>(5000);
    let (multi_path_udp_2nd_tx, multi_path_udp_2nd_rx) = mpsc::channel::<Vec<u8>>(5000);
    let (multi_path_udp_3rd_tx, multi_path_udp_3rd_rx) = mpsc::channel::<Vec<u8>>(5000);

    let th_recv_1 = udp::receiver(recv_addr_1, multi_path_udp_1st_tx);
    let th_recv_2 = udp::receiver(recv_addr_2, multi_path_udp_2nd_tx);
    let th_recv_3 = udp::receiver(recv_addr_3, multi_path_udp_3rd_tx);

    let rx = multi_path_udp_1st_rx.select(multi_path_udp_2nd_rx).select(multi_path_udp_3rd_rx);

    let target_addr: SocketAddr = format!("{}:{}", "127.0.0.1", 20000).parse().unwrap();
    let th_send = udp::sender(rx.filter(filter).map(move |x| {
        let ts = unsafe {
            rtp::timestamp(x.as_ptr())
        };

        (target_addr, x)
    }));

    let _ = th_send.join();
    let _ = th_recv_1.join();
    let _ = th_recv_2.join();
    let _ = th_recv_3.join();
}

fn filter(v: &Vec<u8>) -> bool {
    let ts = unsafe {
        rtp::timestamp(v.as_ptr())
    };

    let contain_flag = {
        (*HASHMAP.read().unwrap()).contains_key(&ts)
    };

    if !contain_flag {
        (*HASHMAP.write().unwrap()).insert(ts, true);
        let len = (*HASHMAP.read().unwrap()).len();
        if len > 100000 {
            (*HASHMAP.write().unwrap()).clear();
        }
    }

    !contain_flag
}


