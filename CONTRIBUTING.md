# Contributing to GraphScore

## License

GraphScore is licensed under the Apache License, Version 2.0. See the
root [LICENSE](LICENSE) file.

Unless you explicitly state otherwise, any contribution intentionally
submitted for inclusion in GraphScore by you shall be licensed under
the Apache License, Version 2.0, without any additional terms or
conditions. You represent that you are legally entitled to make the
contribution under those terms.

## SPDX Header Rule (GraphScore-Owned Source Files Only)

Every GraphScore-owned source file must include exactly one
SPDX-License-Identifier tag on a line by itself near the top:

```
// SPDX-License-Identifier: Apache-2.0
```

or, for non-C++ formats (CMake, Python, shell, etc.):

```
# SPDX-License-Identifier: Apache-2.0
```

The Apache-2.0 license text lives in the root [LICENSE](LICENSE) file.
The SPDX tag is the sole per-file licensing marker.

This rule applies to GraphScore-owned source, build scripts, and test
files only. Third-party, generated, and non-source files (e.g. JSON
data, binary assets, vendored dependency code) must not have SPDX tags
added. Third-party code retains its original license markers.
