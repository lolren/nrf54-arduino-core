# nRF54L15 HAL Parts

`nrf54l15_hal.cpp` includes these fragments in a fixed order. This keeps the
HAL as one translation unit while splitting the former 22k-line monolith into
smaller files for navigation and review.

Do not compile these `.inc` files directly. If a fragment needs to move or be
compiled separately later, first extract the shared anonymous-namespace helpers
and private declarations it depends on.
