// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Serialize;
use serde_json;
use std::fs::File;
use tempfile;

pub fn create_dir<'a, T, S>(iter: T) -> tempfile::TempDir
where
    T: IntoIterator<Item = (&'a str, S)>,
    S: Serialize,
{
    let dir = tempfile::tempdir().unwrap();

    for (name, config) in iter {
        let path = dir.path().join(name);
        let f = File::create(path).unwrap();
        serde_json::to_writer(f, &config).unwrap();
    }

    dir
}
