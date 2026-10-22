The 7zip/lzma wrapper API for the dynworld map volumes, ported verbatim from
the legacy dynworld fork (originally the Ryzom client's own seven_zip copy,
moved to ryzom/common/src by the dynworld patch and trimmed to the wrapper by
this port; see CMakeLists.txt for the rationale).

The upstream 7zip SDK is now provided by nel/3rdparty/seven_zip (NeL::sevenzip)
rather than a second vendored copy, to keep a single set of SDK symbols per
binary. The wrapper was adapted to the 18.05 SDK API: CLookToRead ->
CLookToRead2 (caller-provided look buffer), const-pointer stream callbacks.
