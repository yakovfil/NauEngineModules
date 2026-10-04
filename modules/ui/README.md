# Widget foundation

`NauUi` provides `nau::ui::Widget` through `nau/ui/widget.h`. Link the library directly; it requires no runtime service registration. This new implementation does not load retired Cocos UI assets.

Parents exclusively own ordered children. Each child borrows its parent pointer. Widgets cannot be copied or moved as objects; moving their owning handles preserves addresses and runtime IDs. Names are optional debugging labels, and IDs are process-local rather than persistent asset identifiers.

```cpp
nau::ui::Widget parent;
auto child = std::make_unique<nau::ui::Widget>();
child->setDebugName("child");
auto* borrowed = child.get();
if (!parent.addChild(std::move(child)))
{
    return 1;
}
parent.setVisible(false);
child = parent.removeChild(*borrowed);
```

Successful attachment consumes ownership; validation failure retains the caller's owning handle. An empty child, a child already parented, self-attachment, or attachment of an ancestor is rejected. Never construct another owning handle from a borrowed pointer. Removing a direct child returns its intact subtree and clears its parent; removing a non-child returns null. Sibling order is preserved. `childAt()` returns null for an invalid index, and const access returns const borrowed pointers.

Destroying a parent destroys still-attached descendants. Detached children survive while retained by their owner. Borrowed pointers do not extend lifetime and must not be used after destruction. The parent clears child parent links before destroying children. Hierarchy operations emit no callbacks.

Widgets default to locally visible. `isEffectivelyVisible()` checks the widget and its ancestors immediately; hiding a parent does not rewrite child visibility. Detached locally visible widgets are effectively visible.

Confine access to a tree and its state to one caller-controlled thread. Separate trees may be constructed independently; identity allocation is atomic. Allocation failure follows engine policy rather than the recoverable validation-error contract.

This foundation contains no production derived widgets, layout, input, drawing, ticking, serialization, or editor integration. It depends only on the kernel and its portable dependency closure. Full module builds discover it; existing minimal/scene defaults do not select it.

Native headless tests run through CTest with `-R WidgetTests`. The `tests/consumer` fixture accepts `NAU_WIDGET_ENGINE` for a focused native/Emscripten source build or `NAU_WIDGET_SDK` for an installed SDK. The fixture explicitly adds only the widget module to the standalone minimal kernel.
