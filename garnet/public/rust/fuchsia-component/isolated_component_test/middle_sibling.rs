// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_test_echos::{EchoExposedBySiblingRequest, EchoExposedBySiblingRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
};

fn echo_sibling_server(stream: EchoExposedBySiblingRequestStream) -> impl Future<Output = ()> {
    stream
        .err_into::<failure::Error>()
        .try_for_each(async move |EchoExposedBySiblingRequest::Echo { value, responder }| {
            responder.send(value * 2).context("sending response")?;
            Ok(())
        })
        .unwrap_or_else(|e: failure::Error| panic!("error running echo server: {:?}", e))
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Expose fuchsia.test.echos.EchoExposedBySibling FIDL service that multiplies all inputs by 2.
    let mut fs = ServiceFs::new();
    fs.dir("public").add_fidl_service(|stream| stream);
    fs.take_and_serve_directory_handle()?;

    // spawn server to respond to FIDL requests
    await!(fs.for_each_concurrent(None, echo_sibling_server));

    Ok(())
}
