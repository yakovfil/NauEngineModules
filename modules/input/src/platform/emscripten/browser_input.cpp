// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "browser_input.h"

#include <emscripten.h>

#include "nau/app/core_window_manager.h"
#include "nau/app/platform_window_status.h"
#include "nau/service/service_provider.h"

namespace nau::input
{
    extern "C" EMSCRIPTEN_KEEPALIVE void nau_browser_input_event(BrowserInput* input, int type, int code, float x, float y)
    {
        input->event(type, code, x, y);
    }

    async::Task<> BrowserInput::initService()
    {
        auto* host = getServiceProvider().find<IBrowserWindowHost>();
        if (!host || !host->getBrowserHostId())
            return async::Task<>::makeRejected(NauMakeError("CoreInput browser host is unavailable"));
        std::string keys = "{";
        for (auto key : EnumTraits<Key>::getValues())
        {
            const int value = static_cast<int>(key);
            if (value >= 0 && value < PortableInputState::KeyCount)
                keys += "\"" + std::string(EnumTraits<Key>::toString(key)) + "\":" + std::to_string(value) + ",";
        }
        keys.back() = '}';
        m_accepting = true;
        // All callback installation and removal run synchronously on the UI thread.
        // No C++ lock crosses the proxy; UI callbacks do not dispatch synchronously
        // to the application owner.
        // clang-format off
        const int ready = MAIN_THREAD_EM_ASM_INT({
            const input = $0;
            const host = Module['nauPlatformHosts'] && Module['nauPlatformHosts'].get($1);
            if (!host || !host.element.isConnected) return 0;
            const element = host.element;
            const keys = JSON.parse(UTF8ToString($2));
            const aliases = ({ArrowUp:'Up', ArrowDown:'Down', ArrowLeft:'Left', ArrowRight:'Right',
                Enter:'Return', Backspace:'BackSpace', ShiftLeft:'ShiftL', ShiftRight:'ShiftR',
                ControlLeft:'CtrlL', ControlRight:'CtrlR', AltLeft:'AltL', AltRight:'AltR',
                MetaLeft:'SuperL', MetaRight:'SuperR', Backquote:'Grave', Quote:'Apostrophe',
                Numpad0:'KpInsert', Numpad1:'KpEnd', Numpad2:'KpDown', Numpad3:'KpPageDown',
                Numpad4:'KpLeft', Numpad5:'KpBegin', Numpad6:'KpRight', Numpad7:'KpHome',
                Numpad8:'KpUp', Numpad9:'KpPageUp', NumpadEnter:'KpEnter', NumpadDecimal:'KpDelete',
                NumpadAdd:'KpAdd', NumpadSubtract:'KpSubtract', NumpadMultiply:'KpMultiply',
                NumpadDivide:'KpDivide', NumpadEqual:'KpEqual', Pause:'Break', PrintScreen:'Print'});
            const registrations = [];
            const oldTabindex = element.getAttribute('tabindex');
            let pointer = null;
            let active = true;
            const send = (type, code = 0, x = 0, y = 0) => {
                if (active) _nau_browser_input_event(input, type, code, x, y);
            };
            const reset = () => {
                send(0);
                if (!document.hidden && document.hasFocus() && document.activeElement === element) send(1, keys.Focus, 1);
                if (pointer !== null && element.hasPointerCapture(pointer)) element.releasePointerCapture(pointer);
                pointer = null;
            };
            const listen = (target, event, callback) => {
                target.addEventListener(event, callback);
                registrations.push([target, event, callback]);
            };
            const move = event => {
                const bounds = element.getBoundingClientRect();
                // Convert transformed DOM coordinates to unscaled client/CSS pixels.
                const x = (event.clientX - bounds.left) * element.offsetWidth / (bounds.width || 1) - element.clientLeft;
                const y = (event.clientY - bounds.top) * element.offsetHeight / (bounds.height || 1) - element.clientTop;
                send(3, 0, x, y);
            };
            const observer = new MutationObserver(() => { if (!element.isConnected) reset(); });
            const cleanup = () => {
                active = false;
                for (const [target, event, callback] of registrations) target.removeEventListener(event, callback);
                observer.disconnect();
                if (pointer !== null && element.hasPointerCapture(pointer)) element.releasePointerCapture(pointer);
                pointer = null;
                if (oldTabindex === null) element.removeAttribute('tabindex');
                else element.setAttribute('tabindex', oldTabindex);
                Module['nauInputBindings'].delete(input);
            };
            Module['nauInputBindings'] ||= new Map();
            Module['nauInputBindings'].set(input, cleanup);
            try {
                if (oldTabindex === null) element.tabIndex = 0;
                const key = event => {
                    if (document.activeElement !== element || document.hidden) return;
                    if (event.type === 'keydown' && event.repeat) return;
                    const name = aliases[event.code] || (event.code.startsWith('Key') ? event.code.slice(3) :
                        event.code.startsWith('Digit') ? 'N' + event.code.slice(5) : event.code);
                    const code = keys[name] ?? -1;
                    if (code >= 0) {
                        send(1, code, event.type === 'keydown' ? 1 : 0);
                        if (!event.ctrlKey && !event.metaKey && !event.altKey) event.preventDefault();
                    }
                };
                listen(element, 'keydown', key);
                listen(element, 'keyup', key);
                listen(element, 'focus', () => send(1, keys.Focus, 1));
                listen(document, 'visibilitychange', () => { if (!document.hidden && document.activeElement === element) send(1, keys.Focus, 1); });
                if (!document.hidden && document.activeElement === element) send(1, keys.Focus, 1);
                listen(element, 'pointerdown', event => {
                    if (event.pointerType !== 'mouse' || (pointer !== null && pointer !== event.pointerId)) return;
                    element.focus({preventScroll: true});
                    move(event);
                    pointer = event.pointerId;
                    try {
                        element.setPointerCapture(pointer);
                    } catch (error) {
                        reset();
                        console.error('CoreInput pointer capture failed', error);
                    }
                });
                // Mouse events retain individual button transitions during chords;
                // pointerdown/up alone only cover the first/last held button.
                listen(element, 'mousedown', event => {
                    if (!event.sourceCapabilities?.firesTouchEvents && !document.hidden && document.activeElement === element) send(2, event.button, 1);
                });
                listen(element, 'mouseup', event => { send(2, event.button, 0); });
                listen(element, 'pointermove', event => {
                    if (event.pointerType === 'mouse' && (pointer === null || pointer === event.pointerId)) move(event);
                });
                listen(element, 'pointerup', event => {
                    if (event.pointerType !== 'mouse') return;
                    move(event);
                    if (!event.buttons) {
                        const released = pointer;
                        pointer = null;
                        if (released !== null && element.hasPointerCapture(released)) element.releasePointerCapture(released);
                    }
                });
                listen(element, 'lostpointercapture', () => { if (pointer !== null) reset(); });
                listen(element, 'contextmenu', event => { if (document.activeElement === element) event.preventDefault(); });
                listen(element, 'auxclick', event => { if (document.activeElement === element) event.preventDefault(); });
                listen(element, 'pointercancel', reset);
                listen(element, 'blur', reset);
                listen(window, 'blur', reset);
                listen(window, 'focus', () => { if (!document.hidden && document.activeElement === element) send(1, keys.Focus, 1); });
                listen(document, 'visibilitychange', () => { if (document.hidden) reset(); });
                observer.observe(document, {subtree: true, childList: true});
                return 1;
            } catch (error) {
                cleanup();
                console.error('CoreInput browser binding failed', error);
                return 0;
            }
        }, this, host->getBrowserHostId(), keys.c_str());
        // clang-format on
        if (!ready)
        {
            m_accepting = false;
            return async::Task<>::makeRejected(NauMakeError("CoreInput browser callback installation failed"));
        }
        return async::Task<>::makeResolved();
    }

    async::Task<> BrowserInput::shutdownService()
    {
        m_accepting = false;
        MAIN_THREAD_EM_ASM({
            const cleanup = Module['nauInputBindings'] && Module['nauInputBindings'].get($0);
            if (cleanup)
                cleanup();
        },
                           this);
        m_state.reset();
        m_state.publish(true);
        return async::Task<>::makeResolved();
    }

    void BrowserInput::event(int type, int code, float x, float y)
    {
        if (!m_accepting)
            return;
        if (type == 0)
            m_state.reset();
        else if (type == 1)
            m_state.key(code, x != 0);
        else if (type == 2)
            m_state.button(code, x != 0);
        else if (type == 3)
            m_state.pointer(x, y);
    }

    void BrowserInput::gameInputUpdate(std::chrono::milliseconds)
    {
        m_state.publish(m_firstFrame);
        m_firstFrame = false;
    }
}  // namespace nau::input
