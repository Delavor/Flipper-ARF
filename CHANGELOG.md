# Changelog

---

# Added

## CarEmulate Scene

* Added custom automotive key emulation view with:

  * D-pad button layout
  * TX overlay animation
  * Counter delta display
  * TX power control integration

* Added `CarEmulateSettings` scene with:

  * Enable/disable toggle for the custom emulate UI
  * TX power selector
  * Persistent storage through `SubGhzLastSettings`

### Special Thanks

Special thanks to [RocketGod-git](https://github.com/RocketGod-git?utm_source=chatgpt.com) for inspiration from the Proto Pirate application and related UI/interaction concepts that helped shape the CarEmulate workflow.

---

## Generic Custom Button API (`custom_btn_i.h`)

Protocols can now expose UP/DOWN/LEFT/RIGHT button cycling in the transmitter view using a single macro instead of manually wiring switch/case handlers and scattered `set_original()` / `set_max()` calls.

### New macro

```c id="x0p8ad"
SUBGHZ_CUSTOM_BTN_DEFINE_MAP(
    my_proto,
    {SUBGHZ_CUSTOM_BTN_OK,    0xAA},
    {SUBGHZ_CUSTOM_BTN_UP,    0xBB},
    {SUBGHZ_CUSTOM_BTN_DOWN,  0xCC},
    {SUBGHZ_CUSTOM_BTN_LEFT,  0xDD},
    {SUBGHZ_CUSTOM_BTN_RIGHT, 0xEE},
)
```

The macro automatically generates:

* `my_proto_custom_btn_to_code()`
* `my_proto_code_to_custom_btn()`
* `my_proto_custom_btn_init()`

---

## New Protocols

### Land Rover

* Added support for the Land Rover Sub-GHz protocol.

### Special Thanks

Thanks to [Zero-Mega](https://github.com/Zero-Mega?utm_source=chatgpt.com) for research, references, and contributions related to Land Rover protocol support.

---

# Fixed

## CarEmulate

* Fixed `car_emulate_apply_button()` being fully commented out.

  * D-pad input never reached the `custom_btn` system.
  * Protocols always transmitted the originally captured button regardless of the selected key.

* Fixed `custom_btn_id` potentially being lost if `subghz_tx_start()` internally reset SubGhz state before encoder `deserialize()` execution.

  * The selected button is now passed into `car_emulate_start_tx()`
  * The button is re-applied immediately before TX begins.

* Fixed hardcoded button labels in the CarEmulate view.

  * Previous labels:

    * `LOCK`
    * `UNLOCK`
    * `PANIC`
    * `BOOT`
    * `XXX`
  * Labels are now dynamically stored in the view model and configured by the scene through:

    * `subghz_car_emulate_view_set_labels()`

---

# Internal Changes

## `custom_btn_i.h`

* Added `SubGhzCustomBtnEntry`
* Added `SUBGHZ_CUSTOM_BTN_DEFINE_MAP`
* Added automatic conversion helpers
* Added automatic `_init()` helper generation

---

# Ongoing Work

* Continued improvements and cleanup across additional automotive protocols.
* Ongoing protocol analysis, decoder refinement, encoder validation, and transmit reliability improvements.
* Additional protocol integrations and UI enhancements are still actively being developed.

---

# Notes

The old approach required:

* Manual switch/case handlers
* Manual button registration
* Repeated `subghz_custom_btn_set_original()`
* Repeated `subghz_custom_btn_set_max()`
* Protocol-specific duplicated logic

The new API centralizes all button mapping behavior into a single reusable macro system.
