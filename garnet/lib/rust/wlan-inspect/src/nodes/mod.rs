// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod list;

pub use list::BoundedListNode;

use fidl_fuchsia_inspect as fidl_inspect;
use fuchsia_inspect::{self as finspect, object::ObjectUtil};
use fuchsia_zircon as zx;
use parking_lot::Mutex;
use std::sync::Arc;

pub type SharedNodePtr = Arc<Mutex<finspect::ObjectTreeNode>>;

pub trait NodeExt {
    /// Create and add child to |&self| and return it.
    /// This differs from `add_child` which returns previously existing child node with same name.
    /// For this function, previously existing child node is discarded.
    fn create_child(&mut self, child_name: &str) -> SharedNodePtr;

    fn insert_time_metadata(&mut self, timestamp: zx::Time);
    fn insert_str<S>(&mut self, key: &str, value: S)
    where
        S: Into<String>;
    fn insert_debug<D>(&mut self, key: &str, value: D)
    where
        D: std::fmt::Debug;
}

impl NodeExt for finspect::ObjectTreeNode {
    fn create_child(&mut self, child_name: &str) -> SharedNodePtr {
        let child =
            finspect::ObjectTreeNode::new(fidl_inspect::Object::new(child_name.to_string()));
        let _prev_child = self.add_child_tree(child.clone());
        child
    }

    fn insert_time_metadata(&mut self, timestamp: zx::Time) {
        // TODO(WLAN-1010) - if we have something to post-process Inspect JSON dump, it would be
        //                   better to log the timestamp as MetricValue::UintValue.
        let seconds = timestamp.nanos() / 1000_000_000;
        let millis = (timestamp.nanos() % 1000_000_000) / 1000_000;
        self.add_property(fidl_inspect::Property {
            key: "time".to_string(),
            value: fidl_inspect::PropertyValue::Str(format!("{}.{}", seconds, millis)),
        });
    }

    fn insert_str<S>(&mut self, key: &str, value: S)
    where
        S: Into<String>,
    {
        self.add_property(fidl_inspect::Property {
            key: key.to_string(),
            value: fidl_inspect::PropertyValue::Str(value.into()),
        });
    }

    fn insert_debug<D>(&mut self, key: &str, value: D)
    where
        D: std::fmt::Debug,
    {
        self.insert_str(key, format!("{:?}", value));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_inspect::{self as finspect, object::ObjectUtil};

    #[test]
    fn test_time_metadata_format() {
        let node = finspect::ObjectTreeNode::new_root();
        let timestamp = zx::Time::from_nanos(123_456700000);
        node.lock().insert_time_metadata(timestamp);
        let object = node.lock().evaluate();
        let time_property = object.get_property("time").expect("expect time property");
        assert_eq!(
            time_property,
            &fidl_inspect::Property {
                key: "time".to_string(),
                value: fidl_inspect::PropertyValue::Str("123.456".to_string()),
            }
        );
    }
}