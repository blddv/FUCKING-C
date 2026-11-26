# Third-party software

The WTFPL in the repository root applies to original Fucking C source, documentation, examples, tests, and repository configuration. It does **not** change the licensing of third-party components.

## Boost.Multiprecision

- Used type: `boost::multiprecision::cpp_int`.
- Dependency: Boost 1.74+ headers, obtained from the user's package manager.
- Runtime: no Boost shared library is linked.
- License: **Boost Software License 1.0**, reproduced in [LICENSES/BSL-1.0.txt](LICENSES/BSL-1.0.txt).
- Upstream: https://www.boost.org/doc/libs/release/libs/multiprecision/

Boost headers are not vendored. Keep the Boost notice with distributed packages that include compiled Boost code. System compiler/runtime libraries remain under their respective licenses.
