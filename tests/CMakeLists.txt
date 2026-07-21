# tests/ — cross-module tests that don't belong to a single module.
#
# Per-module unit tests live next to their module (src/<mod>/<mod>_test.c) and
# are registered there. This tree holds tests that span modules:
#
#   integration/  — exercise several modules together through the public ABI.
#   e2e/          — end-to-end tiny-model self-tests (the token-exact oracle gate
#                   lands here once a forward pass exists; DD-004, §3.15).
#   property/     — invariant/property tests (e.g. the streaming coalesced-read
#                   contract, DD-005).
#
# All targets use CTest. During bootstrap these assert only that the public ABI
# links and reports its version — real coverage grows with the engine.

add_subdirectory(integration)
add_subdirectory(property)
add_subdirectory(e2e)
