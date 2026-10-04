# NauWidget foundation

`NauUi` provides `nau::ui::NauWidget` through `nau/ui/nau_widget.h`. It derives from `nau::scene::NauObject`, supports engine RTTI, and links publicly to `CoreScene` and its portable assets/kernel dependencies. Creating and using a widget needs no window, GPU, application service, or scene-manager startup.

Create widgets through `NauWidget::create(allocator)`; the allocator is optional and defaults to the engine allocator. Keep a supplied allocator alive until all widgets allocated through it have been destroyed. The factory returns `nau::scene::ObjectUniquePtr<NauWidget>`. Stack construction, `std::make_unique`, ordinary `delete`, and shared ownership are unsupported. A construction key restricts direct construction; test-only subclasses use the same allocator-aware object factory with their concrete type.

Parents exclusively own ordered children through engine object pointers. Each child keeps a weak parent reference. Widgets cannot be copied or moved as objects; moving owning handles preserves their addresses and inherited `getUid()`. Fresh widgets receive distinct non-null UIDs. Name, visibility, and hierarchy operations retain UID. Inherited `setUid()` is available, but explicit reassignment falls outside this identity guarantee.

```cpp
#include "nau/ui/nau_widget.h"

auto parent = nau::ui::NauWidget::create();
auto child = nau::ui::NauWidget::create();
child->setDebugName("child");
auto childRef = child.getRef();
if (!parent->addChild(std::move(child)))
{
    return 1;
}
parent->setVisible(false);
child = parent->removeChild(*childRef);
parent.reset(); // Detached child and its weak reference survive.
child.reset();  // childRef is now invalid.
```

Successful attachment consumes ownership; validation failure retains the caller's handle. An empty child, a child already parented, self-attachment, or attachment of an ancestor is rejected. Never manufacture a second owner from a borrowed pointer. Removing a direct child returns its intact subtree and clears its parent; removing a non-child returns empty ownership. Sibling order is preserved. `childAt()` returns null for an invalid index, and const access returns const borrowed pointers.

Release or reset the exclusive owner to destroy a widget. To destroy an attached subtree, remove it and release the returned owner. Root release destroys all attached descendants through their recorded concrete destructors and allocators. Parent references are cleared before child destruction. Engine weak references become invalid before their target's concrete destructor runs; they do not extend lifetime and remain valid across detachment and reattachment. Raw borrowed pointers must not be used after destruction.

`destroy()` is private and final on NauWidget. Calling it through normal virtual dispatch on `NauObject` terminates with `std::abort()` in every build, including Release. There is no self-removal or scene-manager destruction behavior. Explicit qualified calls to `NauObject::destroy()` bypass the override and are unsupported; the inherited assertion guard is disabled in Release.

Widgets default to locally visible and unnamed. `isEffectivelyVisible()` checks the widget and its ancestors immediately; hiding a parent does not rewrite child visibility. Detached locally visible widgets are effectively visible. Confine tree/state and weak-reference access to one caller-controlled thread; weak references provide no synchronization.

This foundation has no production derived widgets, layout, input, drawing, ticking, persistence, scene attachment, or editor integration. Widget weak references are in-memory references: the existing SceneQuery serializer/resolver does not support widgets. Save/load and scene attachment will be separate changes.

Full module builds discover NauUi; standalone kernel and existing minimal/scene defaults do not select it. The `tests/consumer` fixture explicitly selects UI with a focused portable scene/assets graph independent of NauDemo. Set `NAU_WIDGET_ENGINE` to the standalone engine source for native/Emscripten source builds, or `NAU_WIDGET_SDK` to a relocated installed SDK. Enable `NAU_WIDGET_TESTS=ON` for native headless ownership/state tests; run CTest in Debug and Release. No application startup is needed.

Migration: replace `Widget`/`nau/ui/widget.h` with `NauWidget`/`nau/ui/nau_widget.h`, direct construction with `create()`, standard unique pointers with engine owners, and `id()` with `getUid()`. No legacy type or header alias is retained.
