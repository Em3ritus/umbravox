// router.h -- the engine's interaction router: a committed tap (one uieng::Hit)
// maps to a screen transition plus the UI-state changes and one-shot side-effect
// requests it implies. Pure and host-testable; depends only on layout_engine.h
// (the Action/ScreenId/Hit vocabulary), never on drawing or State.
//
// The transition/touch gate (scanpres::Transition/TouchGate) decides WHETHER a
// tap is delivered; this decides WHAT a delivered tap does. Opening a device
// detail remembers the originating list and its scroll so BACK restores both
// (contract section 3: "Returning from detail preserves list scope and scroll").
// Side effects the device must perform (toggle logging, mesh enroll/reset,
// setting step/reset, theme step, delete a log) are returned as one-shot
// Requests for the caller to act on and discard -- the router never touches
// radios, the card, or Settings itself.
#pragma once
#include "layout_engine.h"
#include "domain_range.h"

namespace uinav {

// The full set of engine screens the router can rest on. The first nine share
// uieng::ScreenId's order (so a Nav payload casts across); Detail and Files are
// separate screen values; Files also has an explicit MENU Nav launcher.
enum class Screen : int { Scan, Domain, Codex, Menu, Mesh, Omens, Echoes, Attune, Aspect, Detail, Files };
inline Screen screenFromNav(int payload) {         // Nav payload (a ScreenId) -> Screen
    if(payload==(int)uieng::ScreenId::Files)return Screen::Files;
    if (payload < 0 || payload > (int)Screen::Aspect) return Screen::Scan;
    return (Screen)payload;
}

// CODEX scope: 0/1/2 are the band filters; 3 is the located scope (LOCATED).
static constexpr int kCodexLocated = 3;

// One-shot side effects a dispatch raised. The caller performs them against the
// radios/card/Settings and then discards this -- none persists in the router.
struct Requests {
    bool markPoi = false;
    bool logToggle   = false;   // ToggleLogging: flip the logger
    bool openGps     = false;   // OpenGps: open GPS diagnostics (no engine screen yet)
    bool powerDetail = false;   // PowerDetail: power detail (NM: sensing unavailable)
    bool meshEnroll  = false;   // open a pairing window
    bool meshReset   = false;   // return the fleet to the open network
    bool settingReset= false;   // CLEAR: reset Settings to defaults
    bool fileDelete  = false;   // a confirmed delete of the selected log
    int  settingStep = 0;       // ATTUNE: encoded +/-(knob+1), 0 = none
    int  themeStep   = 0;       // ASPECT: -1/+1 theme step, 0 = none
    int  sweepStep   = 0;       // ASPECT: -1/+1 sweep step (SettingStep payload +/-2)
    int  alertStep   = 0;       // ASPECT: -1/+1 alert-style step (payload +/-3)
    int  rangeDir    = 0;       // DOMAIN: -1/+1 range step applied this tick
};

class Router {
public:
    // --- observable UI state -------------------------------------------------
    Screen screen() const { return scr_; }
    int  codexScope() const { return codexScope_; }   // band 0/1/2 or kCodexLocated
    int  codexScroll() const { return codexScroll_; }
    int  rangeIdx() const { return rangeIdx_; }
    int  attunePage() const { return attunePage_; }
    bool labelsPlain() const { return labelsPlain_; }
    int  fileSel() const { return fileSel_; }
    bool fileConfirm() const { return fileConfirm_; }
    Screen detailOrigin() const { return detailOrigin_; }
    const char* detailKey() const { return detailKey_; }

    // Sync the router's current screen to an EXTERNAL change (e.g. the device
    // opened a screen by a path other than a tap). Leaves detailOrigin and the
    // preserved scope/scroll untouched, so a later Back still returns correctly.
    void setScreen(Screen s) { scr_ = s; }

    // The caller owns the live scroll; it writes it back as the finger drags so
    // the value the router preserves across a DETAIL round-trip is current.
    void setCodexScroll(int s) { codexScroll_ = s < 0 ? 0 : s; }
    void setFileScroll(int s)  { fileScroll_  = s < 0 ? 0 : s; }
    int  fileScroll() const    { return fileScroll_; }

    // Number of range rungs the clamp is bounded by (kept in sync with the
    // renderer's ladder; see domr::kRanges). Set once at construction.
    explicit Router(int nRangeRungs = domr::kNRanges) : nRange_(nRangeRungs < 1 ? 1 : nRangeRungs) {}

    // --- dispatch a committed tap -------------------------------------------
    Requests dispatch(const uieng::Hit& h) {
        using A = uieng::Action;
        Requests r;
        switch (h.action) {
            case A::Nav:            scr_ = screenFromNav(h.payload); if(scr_==Screen::Files)enteringFiles(); break;
            case A::DetailPage: break;
            case A::ClientPage: break;
            case A::EchoPage: break;
            case A::MeshPage: break;
            case A::CodexPage: break; // caller owns the visible page size
            case A::CodexFilter:    codexScope_ = h.payload; scr_ = Screen::Codex; break;
            case A::OpenLocatedCodex: codexScope_ = kCodexLocated; scr_ = Screen::Codex; break;

            case A::OpenDeviceDetail:
            case A::OpenThreatDetail:
                detailOrigin_ = scr_;              // remember the list we came from...
                copyKey_(h.key);                   // ...and the device identity
                scr_ = Screen::Detail;
                break;
            case A::Back:
                scr_ = detailOrigin_;              // scope + scroll were never touched
                break;

            case A::RangeStep: {
                const int ni = rangeIdx_ + (h.payload < 0 ? -1 : 1);
                rangeIdx_ = ni < 0 ? 0 : (ni >= nRange_ ? nRange_ - 1 : ni);
                r.rangeDir = h.payload < 0 ? -1 : 1;
                break;
            }
            case A::ToggleLabels:   labelsPlain_ = !labelsPlain_; break;

            case A::SettingStep:
                // ATTUNE and ASPECT both emit SettingStep with overlapping
                // payloads, so disambiguate by the screen we are on, never by
                // the payload magnitude.
                if (h.payload == 9) {
                    r.settingStep = h.payload; // Shared BRIDGE toggle on every screen.
                } else if (scr_ == Screen::Aspect) {
                    if (h.payload == 2 || h.payload == -2)      r.sweepStep = h.payload < 0 ? -1 : 1;
                    else if (h.payload == 3 || h.payload == -3) r.alertStep = h.payload < 0 ? -1 : 1;
                } else {
                    r.settingStep = h.payload;                 // ATTUNE knob: +/-(knob+1)
                }
                break;
            case A::SettingPage:    attunePage_ = h.payload < 0 ? 0 : h.payload; break;
            case A::SettingReset:   r.settingReset = true; break;
            case A::ThemeStep:      r.themeStep = h.payload < 0 ? -1 : 1; break;
            case A::OrientToggle:   /* reserved; no-op in the engine prototype */ break;

            case A::MeshEnroll:     r.meshEnroll = true; break;
            case A::MeshResetNet:   r.meshReset  = true; break;

            case A::FileSelect:     fileSel_ = h.payload; break;        // visible index; caller adds scroll
            case A::FileDelete:     fileConfirm_ = true; break;         // open the modal
            case A::FileConfirmYes: r.fileDelete = true; fileConfirm_ = false; break;
            case A::FileConfirmNo:  fileConfirm_ = false; break;

            case A::OpenGps:        r.openGps = true; break;
            case A::OpenMesh:       scr_ = Screen::Mesh; break;
            case A::PowerDetail:    r.powerDetail = true; break;
            case A::MarkPoi: r.markPoi = true; break;
            case A::ToggleLogging:  r.logToggle = true; break;
        }
        return r;
    }

    // A screen change discards a stale selection the new screen should not keep
    // (the file modal must never survive leaving FILES, say). The caller calls
    // this right after a transition it recognises as a context switch.
    void enteringFiles() { fileConfirm_ = false; fileSel_ = -1; }

private:
    void copyKey_(const char* k) {
        int i = 0; for (; k && k[i] && i < (int)sizeof(detailKey_) - 1; i++) detailKey_[i] = k[i];
        detailKey_[i] = 0;
    }
    Screen scr_ = Screen::Scan;
    Screen detailOrigin_ = Screen::Scan;
    char   detailKey_[32] = {0};
    int    codexScope_ = 0, codexScroll_ = 0;
    int    rangeIdx_ = 1;          // native DOMAIN default rung: 25m
    int    attunePage_ = 0;
    int    fileSel_ = -1, fileScroll_ = 0;
    bool   labelsPlain_ = false;
    bool   fileConfirm_ = false;
    int    nRange_ = domr::kNRanges;
};

}  // namespace uinav
