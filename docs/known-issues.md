# Known Issues & Workarounds

This document tracks known issues with dependencies and platform-specific bugs that affect Beebium development.

## Protobuf Map Hash Bug (x86_64 macOS)

**Status**: Active workaround in place
**Affected platforms**: x86_64 macOS (not arm64)
**Affected versions**: protobuf v22 and later

### Problem

`google::protobuf::Map::find()`, `count()`, and `at()` fail to locate keys that exist and are correctly accessible via iteration. This manifests on x86_64 macOS but not arm64.

### Root Cause

Since protobuf v22, `google::protobuf::Map` uses `absl::hash` with a non-deterministic seed. After gRPC serialization/deserialization, the hash buckets can become inconsistent between the sender and receiver, causing hash-based lookups to fail even though the data is intact.

### Symptoms

When accessing a deserialized `google::protobuf::Map`:

- `map.size()` returns the correct count (e.g., 4)
- Iterating with a range-for loop shows all correct keys and values
- `map.find(key)` returns `end()` even for keys that exist
- `map.count(key)` returns 0 even for keys that exist
- `map.at(key)` throws/aborts with "key not found"

### Diagnostic Output Example

```
gRPC metadata size(): 4
gRPC metadata contents:
  [0] key='related_key' value='Caps Lock'
  [1] key='shape' value='domed'
  [2] key='color' value='625nm'
  [3] key='label' value='CAPS LOCK'

Testing find/count for 'label':
  grpc_meta.count("label") = 0           # WRONG - should be 1
  grpc_meta.find("label") == end(): 1    # WRONG - should be 0

  'label' == "label": 1                  # String comparison works!
```

### Workaround

Use iteration-based lookup instead of hash-based lookup. Helper functions are defined in `tests/test_grpc_indicator.cpp`:

```cpp
// Find a value in a protobuf Map using iteration
template<typename MapType>
auto proto_map_find(const MapType& map, const typename MapType::key_type& key)
    -> decltype(map.begin()) {
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it->first == key) {
            return it;
        }
    }
    return map.end();
}

// Check if a key exists in a protobuf Map using iteration
template<typename MapType>
bool proto_map_contains(const MapType& map, const typename MapType::key_type& key) {
    return proto_map_find(map, key) != map.end();
}

// Get a value from a protobuf Map using iteration, with default
template<typename MapType>
auto proto_map_get(const MapType& map, const typename MapType::key_type& key,
                   const typename MapType::mapped_type& default_value = {})
    -> typename MapType::mapped_type {
    auto it = proto_map_find(map, key);
    return (it != map.end()) ? it->second : default_value;
}
```

### Affected Code

Any C++ code that:
1. Receives a `google::protobuf::Map` via gRPC (after deserialization)
2. Uses `find()`, `count()`, or `at()` for key lookup

Currently worked around in:
- `tests/test_grpc_indicator.cpp` - indicator service tests

May need similar treatment in:
- macOS client code (Swift interop with C++ protobuf)
- Any future C++ gRPC clients

### References

- https://github.com/protocolbuffers/protobuf/issues/15069 - "find() fails on deserialized Struct fields"
- https://github.com/protocolbuffers/protobuf/issues/18097 - "Map across DLLs leads to crashes" (explains absl::hash seed issue)
- https://github.com/protocolbuffers/protobuf/issues/13543 - "map data structure error"

### Investigation Notes

The bug was discovered in February 2026 when indicator service tests failed on x86_64 macOS CI but passed on arm64. Extensive diagnostics confirmed:

1. Data integrity is fine (iteration shows correct values)
2. String comparison works (`key == "label"` returns true)
3. Only hash-based operations fail
4. The issue is specific to x86_64; arm64 builds work correctly

This suggests the hash function implementation or seed initialization differs between architectures in a way that breaks cross-architecture or post-deserialization consistency.
