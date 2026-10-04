# NauWidget scene foundation

`NauUi` provides `nau::ui::NauWidget` in `nau/ui/nau_widget.h`. It derives from `nau::scene::SceneObject` and uses the scene hierarchy, name, transform, components, identity and lifecycle. It adds visibility, rectangle size and pivot through its `NauWidgetRootComponent`. There is no layout, input, rendering or editor integration.

## Creation and registration

`NauWidget::create(allocator)` returns `ObjectUniquePtr<NauWidget>`, convertible to a scene-object owner. Its optional allocator owns both widget and root-component allocations and must outlive them. Moving owners preserves object addresses; copying/moving concrete objects and ordinary deletion are unsupported.

Direct creation, local state access and release of an inactive owner need no application startup. Scene factory construction/loading, hierarchy operations which inspect activation, queries and explicit `destroy()` need initialized scene services. Normal CoreScene module initialization supplies them; headless consumers must initialize those services too. Async scene loading needs an executor which the caller pumps.

Call `nau::ui::registerWidgetComponents()` before the scene factory first enumerates component types. Normal NauUi module initialization calls it. Static consumers must call the entry point or explicitly initialize the module; registration does not depend on a static initializer. Do not register after the factory has cached its component list. With registration, `factory.createSceneObject<NauWidgetRootComponent>()` returns an object with NauWidget RTTI and exactly one root component.

## Hierarchy and transforms

Parents own ordered children; children keep the normal non-owning scene parent link. Ordinary scene objects and widgets can share the same tree. Confine hierarchy and state access to one caller-controlled thread.

```cpp
// Scene services have already been initialized.
auto parent = nau::ui::NauWidget::create();
auto child = nau::ui::NauWidget::create();
child->setName("caption");
child->setTranslation({20.f, 10.f, 3.f});
if (!child->setSize({200.f, 100.f}) || !child->setPivot({0.5f, 0.5f}))
{
    return 1;
}
auto& attached = parent->attachChild(std::move(child));
auto worldMatrix = attached.getWorldTransform().toMatrixWithScale();
```

`attachChild` preserves the child's local transform. Use `attachChildAsync` when awaiting activation is needed. `setParent(newParent)` preserves world placement; `SetParentOpts::DontKeepWorldTransform` preserves the local transform. `removeChild` destroys the child and returns no owner. To retain a subtree, reparent it; there is no detach-to-owner API.

Inherited `destroy()` invalidates weak references using scene rules. An inactive object still held by an owner is physically deleted when the owner is released. Active destruction invalidates references immediately and lets outstanding component deactivation finish before deleting components. Destroying the special scene root directly is unsupported.

Use `getTransform/setTransform`, component translation/rotation/scale setters, and `getWorldTransform`. These retain the existing scene `math::Transform` composition and validation behavior, including its handling of rotation with nonuniform scale, singular scales and floating-point limits. They do not promise an exact accumulated affine matrix preserving shear.

## Rectangle and visibility

`size()` defaults to `(0,0)`, accepts finite nonnegative dimensions and returns `Result<>` from `setSize`. `pivot()` defaults to `(0.5,0.5)` and accepts finite coordinates, including values outside `[0,1]`. Failed setters preserve prior state. The local XY rectangle at Z=0 is:

```text
minimum = (-pivot.x * size.x, -pivot.y * size.y, 0)
maximum = ((1 - pivot.x) * size.x, (1 - pivot.y) * size.y, 0)
```

The parent's pivot is the child coordinate origin. Size/pivot edits change geometry around that origin without changing transforms or descendant origins. An ordinary scene ancestor contributes its normal scene origin.

Local visibility defaults to true. `isEffectivelyVisible()` visits every scene ancestor and combines visibility of widget ancestors, including across ordinary objects. Ordinary objects have no UI visibility flag. Visibility does not change scene activation.

## Scene and prefab persistence

The root-component type identifies NauWidget during construction; there is no new scene container format or widget UID index. Scenes support widgets beneath the ordinary scene root; prefab roots can be widgets. Existing scene save/load, `clone()`, object-category queries and serialized component weak references work with widgets. Ordinary load preserves saved UIDs; prefab instantiation, cloning and `RecreateUid` generate new object/component UIDs and remap references. Old live weak handles never retarget loaded objects.

The root component serializes `widgetState` with `visible`, `size` and `pivot`. Assign the group through `setValue("widgetState", value)` to validate a complete candidate before committing it; snapshots from `getValue` cannot mutate live state. Missing state/fields use defaults when restoring a fresh widget. Wrong types, explicit null state, non-finite geometry and negative size fail without clamping.

CoreScene's optional `IComponentPropertyValidator` examines the original property record before runtime assignment, including explicit null values. Failure skips property assignment, logs a restoration diagnostic and continues normal UID/transform restoration. Thus a newly loaded widget retains default widget state on failure. Generic `RuntimeValue::assign` retains its existing null-skipping semantics; callers restoring whole component records outside the scene loader should invoke validation first.

Without UI registration the scene factory restores an ordinary object with a missing root component. Its unknown type and properties survive re-save; loading that data with UI registered recovers NauWidget. The placeholder is not an operational widget.

The focused `NauSceneSerialization` library exposes the existing portable scene container reader/writer through `nau/assets/scene_serialization.h`, without importing platform, mesh or image tools. See the public consumer's `main.cpp` for an executable scene-stream round trip.

## API migration and validation

| Previous widget API | Scene API |
| --- | --- |
| `debugName/setDebugName` | `getName/setName` |
| `parent` | `getParentObject` (may be an ordinary object) |
| `childCount/childAt` | `getDirectChildObjects` |
| `addChild` | `attachChild/attachChildAsync` |
| owning `removeChild` result | destroying `removeChild`; use `setParent` to retain a subtree |
| `localTransform/setLocalTransform` | `getTransform/setTransform` |
| `accumulatedMatrix` | `getWorldTransform().toMatrixWithScale()` with scene transform semantics |
| owner-only destruction | normal scene `destroy()` and owner release |

Rebuild consumers for the class-layout and API change. Full module builds select NauUi; standalone kernel and ordinary minimal/scene defaults remain UI independent. The `tests/consumer` fixture explicitly selects UI with a portable graph independent of NauDemo. Configure `NAU_WIDGET_ENGINE` for a source build or `NAU_WIDGET_SDK` for a relocated SDK; enable `NAU_WIDGET_TESTS=ON` for native regression tests. Run CTest in Debug/Release or the Emscripten consumer under Node.
