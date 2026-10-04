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

## Spatial state

Each widget stores an engine `math::Transform` relative to its parent's pivot. `localTransform()` returns a const reference; edit a copy and submit it with `setLocalTransform()`. The initial transform is identity. Scale, then quaternion rotation, then translation maps local points into parent coordinates. Translation places this widget's pivot at that position.

`size()` is unscaled width/height, initially `(0, 0)`. `pivot()` is a dimensionless rectangle coordinate, initially `(0.5, 0.5)`. The widget rectangle lies in its local XY plane at Z=0:

```text
minimum = (-pivot.x * width, -pivot.y * height, 0)
maximum = ((1 - pivot.x) * width, (1 - pivot.y) * height, 0)
```

For size `(200, 100)` and centered pivot, the rectangle spans `(-100, -50, 0)` to `(100, 50, 0)`. Pivot `(0, 0)` denotes its minimum-coordinate corner; `(1, 1)` denotes its maximum-coordinate corner. An external pivot `(2, -1)` gives minimum `(-400, 100, 0)` and maximum `(-200, 200, 0)` for that size. Coordinates use engine axes and caller-defined units; screen orientation and pixel conversion belong to future presentation.

The parent's pivot is the child coordinate origin. Changing size or pivot reshapes/repositions the rectangle around its fixed origin, leaving the widget transform and descendant origins unchanged. Changing scale transforms the widget geometry and descendants together. Attachment and detachment retain local transform, size, and pivot; accumulated placement follows the new ancestry.

`accumulatedMatrix()` returns the ordered product of ancestor local matrices and this widget's local matrix, immediately reflecting edits. It maps pivot-relative points into the coordinate space outside the top-level widget. Parentless widgets return their own local matrix. Composition preserves shear produced by nonuniform scaling and rotation. Queries walk ancestors in O(depth) time and O(1) temporary storage.

```cpp
auto widget = nau::ui::NauWidget::create();
auto transform = widget->localTransform();
transform.setTranslation({20.f, 10.f, 3.f});
if (!widget->setLocalTransform(transform) || !widget->setSize({200.f, 100.f}) ||
    !widget->setPivot({0.5f, 0.5f}))
{
    return 1;
}
auto matrix = widget->accumulatedMatrix();
```

Spatial setters return `Result<>`: invalid input reports failure and preserves existing state in every build. Transform components must be finite and rotation must pass the engine's normalized-quaternion check (squared-length tolerance `1e-3`). Size must be finite and nonnegative; zero is valid. Pivot must be finite, including values outside `[0, 1]`. Finite zero/negative scale is supported. Values are stored without clamping or automatic normalization. Ordinary floating-point range limits apply to derived arithmetic. Rebuild consumers when updating the library: spatial storage changes the widget object size.

Migration: replace `Widget`/`nau/ui/widget.h` with `NauWidget`/`nau/ui/nau_widget.h`, direct construction with `create()`, standard unique pointers with engine owners, and `id()` with `getUid()`. No legacy type or header alias is retained.
