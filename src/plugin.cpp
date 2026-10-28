#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <map>
#include <memory>
#include <cstring>
#include <string>
#include "tail.h"
#include "version.h"

// Minimal, public Notepad++ plugin ABI. No SDK binaries required.
struct NppData { HWND npp, main, second; };
struct ShortcutKey { bool ctrl, alt, shift; UCHAR key; };
struct FuncItem {
    wchar_t name[64]; void (__cdecl *command)(); int id;
    bool checked; ShortcutKey* shortcut;
};
namespace sci {
enum : UINT {
    GetStyleAt=2010, GetLine=2153, GetLineCount=2154, PositionFromLine=2167,
    LineLength=2350, GetLexer=4002, GetLastChild=2224, GetLineVisible=2228,
    GetFoldExpanded=2230, ToggleFold=2231, GetFoldLevel=2223,
    GetFirstVisibleLine=2152, LinesOnScreen=2370, DocLineFromVisible=2221,
    ToggleFoldShowText=2700, FoldDisplayTextSetStyle=2701,
    FoldDisplayTextGetStyle=2707, GetDocPointer=2357
};
}
static NppData npp{};
static bool enabled = true;
static bool showFirstComment = false;
static bool showLastComment = false;
struct PreviewBitmap {
    HBITMAP bitmap{};
    HWND overlay{};
    LOGFONTW font{};
    COLORREF foreground{}, commentColour{}, background{};
    COLORREF markerForeground{}, markerBackground{};
    int width{}, height{};
    ~PreviewBitmap() {if(overlay) DestroyWindow(overlay);if(bitmap) DeleteObject(bitmap);}
};
struct Visual {
    bool visible=true;
    RECT mask{}, dots{};
    int x{}, y{}, height{}, style{};
    int commentStyle{};
    size_t commentStart{};
    LRESULT selectionTrigger{}, blockEnd{};
    std::wstring text, tip;
    LRESULT doc{};
    std::unique_ptr<PreviewBitmap> cache;
};
struct View {
    HWND window{};
    UINT_PTR timer{};
    LRESULT doc{};
    int originalStyle{};
    bool busy{};
    bool preparingPaint{};
    bool presentationActive{};
    bool settingFoldFlags{};
    bool addedClipChildren{};
    WPARAM originalFoldFlags=16; // Notepad++ default: line after contracted fold.
    bool coloursCaptured{};
    bool colourSet[2]{};
    LRESULT colours[2]{};
    std::map<LRESULT, Visual> visuals;
    HWND tooltip{};
    LRESULT pressed=-1;
};
static View views[2];
static LRESULT send(HWND w, UINT m, WPARAM a=0, LPARAM b=0) {
    return SendMessage(w,m,a,b);
}
static void suppressLines(View& v) {
    for(int i=0;i<2;++i) {
        auto element=80+i; // SC_ELEMENT_FOLD_LINE / SC_ELEMENT_HIDDEN_LINE.
        bool set=send(v.window,2756,element)!=0; // SCI_GETELEMENTISSET
        auto colour=send(v.window,2754,element); // SCI_GETELEMENTCOLOUR
        if(!v.coloursCaptured) { v.colourSet[i]=set; v.colours[i]=colour; }
        if(!set || colour!=0) send(v.window,2753,element,0); // transparent RGBA
    }
    v.coloursCaptured=true;
}
static void restoreLines(View& v) {
    if(!v.coloursCaptured) return;
    for(int i=0;i<2;++i) {
        if(v.colourSet[i]) send(v.window,2753,80+i,v.colours[i]);
        else send(v.window,2755,80+i); // SCI_RESETELEMENTCOLOUR
    }
    v.coloursCaptured=false;
}
static bool applyPresentation(View& v) {
    const bool active=enabled && foldtail::supported(static_cast<int>(send(v.window,sci::GetLexer)));
    if(active!=v.presentationActive) {
        send(v.window,sci::FoldDisplayTextSetStyle,active?0:v.originalStyle);
        v.settingFoldFlags=true;
        send(v.window,2233,active?0:v.originalFoldFlags);
        v.settingFoldFlags=false;
        v.presentationActive=active;
        InvalidateRect(v.window,nullptr,FALSE);
    }
    if(active) suppressLines(v); else restoreLines(v);
    return active;
}
static std::string readLine(HWND w, LRESULT line) {
    auto n = send(w,sci::LineLength,line);
    if (n <= 0 || n > 8192) return "";
    std::string s(static_cast<size_t>(n)+1,'\0');
    auto copied=send(w,sci::GetLine,line,reinterpret_cast<LPARAM>(s.data()));
    s.resize(static_cast<size_t>(std::max<LRESULT>(0,copied)));
    return s;
}
#include "css.h"
struct TailInfo { std::string suffix, raw; LRESULT line=-1; };
static TailInfo label(HWND w, LRESULT header,int lexer) {
    if (!foldtail::supported(lexer)) return {};
    auto last=send(w,sci::GetLastChild,header,-1);
    // Stay strictly inside the hidden range: do not duplicate visible closing lines.
    for (auto line=last; line>header && line>=last-8; --line) {
        if (send(w,sci::GetLineVisible,line)) return {};
        auto s=readLine(w,line);
        auto pos=send(w,sci::PositionFromLine,line);
        bool code=false;
        for(size_t i=0;i<s.size();++i)
            if (!std::isspace(static_cast<unsigned char>(s[i])) &&
                !foldtail::comment(lexer,syntaxStyle(w,pos+i,lexer)))
                code=true;
        if (!code) continue;
        auto result=foldtail::tail(s,lexer,[&](size_t i){
            return syntaxStyle(w,pos+i,lexer);
        });
        return {result,s,line};
    }
    return {};
}
#include "visual.h"
static void extendFoldSelection(View& v) {
    if(!enabled || send(v.window,2570)!=1 || send(v.window,2423)!=0) return; // Single stream selection only.
    auto anchor=send(v.window,2009), caret=send(v.window,2008);
    if(anchor==caret) return;
    auto start=std::min(anchor,caret),end=std::max(anchor,caret);
    for(const auto& entry:v.visuals) {
        const auto& visual=entry.second;
        if(!visual.visible) continue;
        if(send(v.window,sci::GetFoldExpanded,entry.first)) continue;
        if(start<visual.selectionTrigger && end>=visual.selectionTrigger && end<visual.blockEnd)
            end=visual.blockEnd;
    }
    if(end!=std::max(anchor,caret)) {
        // Select actual source bytes, never the synthetic display text.
        if(caret>anchor) send(v.window,2160,anchor,end); // SCI_SETSEL
        else send(v.window,2160,end,caret);
    }
}
static const Visual* collapsedVisual(View& v,LRESULT line) {
    auto found=v.visuals.find(line);
    if(found==v.visuals.end() || !found->second.visible ||
        send(v.window,sci::GetFoldExpanded,line)) return nullptr;
    return &found->second;
}
static void placeCaret(HWND w,LRESULT position) {
    send(w,2160,position,position); // SCI_SETSEL: no source text is changed.
    send(w,2169); // SCI_SCROLLCARET
}
static void placeCaretAfterFold(View& v,LRESULT line,const Visual& visual) {
    const auto visible=send(v.window,2220,line); // SCI_VISIBLEFROMDOCLINE
    const auto next=send(v.window,sci::DocLineFromVisible,visible+1);
    placeCaret(v.window,next>line && next<send(v.window,sci::GetLineCount)?
        send(v.window,sci::PositionFromLine,next):visual.selectionTrigger);
}
static bool navigateFold(View& v,WPARAM key) {
    if(send(v.window,2570)!=1 || send(v.window,2423)!=0 ||
        send(v.window,2008)!=send(v.window,2009)) return false;
    const auto caret=send(v.window,2008);
    const auto line=send(v.window,2166,caret); // SCI_LINEFROMPOSITION
    const auto visual=collapsedVisual(v,line);
    if(visual && (key==VK_END || (key==VK_RIGHT && caret>=visual->selectionTrigger))) {
        placeCaretAfterFold(v,line,*visual);
        return true;
    }
    if(key==VK_LEFT && caret==send(v.window,sci::PositionFromLine,line)) {
        const auto visible=send(v.window,2220,line);
        if(visible>0) {
            const auto previous=send(v.window,sci::DocLineFromVisible,visible-1);
            if(const auto before=collapsedVisual(v,previous)) {
                placeCaret(v.window,before->selectionTrigger);
                return true;
            }
        }
    }
    return false;
}
static void clampFoldCaret(View& v) {
    if(send(v.window,2570)!=1 || send(v.window,2423)!=0 ||
        send(v.window,2008)!=send(v.window,2009)) return;
    const auto caret=send(v.window,2008);
    if(const auto visual=collapsedVisual(v,send(v.window,2166,caret)))
        if(caret>visual->selectionTrigger) placeCaret(v.window,visual->selectionTrigger);
}
static LRESULT CALLBACK subclass(HWND w,UINT msg,WPARAM a,LPARAM b,UINT_PTR id,DWORD_PTR ref) {
    auto& v=*reinterpret_cast<View*>(ref);
    if(msg==WM_PAINT) {
        // Place child overlays before Scintilla paints, so its clipping region
        // already excludes their new positions after a scroll.
        if(!v.preparingPaint) {
            v.preparingPaint=true;
            try { update(v); paintVisuals(v); } catch (...) { v.busy=false; }
            v.preparingPaint=false;
        }
        return DefSubclassProc(w,msg,a,b);
    }
    const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0;
    const bool alt=(GetKeyState(VK_MENU)&0x8000)!=0;
    const bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    if(enabled && msg==WM_KEYDOWN && !control && !alt &&
        (a==VK_LEFT || a==VK_RIGHT || a==VK_UP || a==VK_DOWN || a==VK_HOME || a==VK_END)) {
        try {
            update(v);
            if(!shift && navigateFold(v,a)) {paintVisuals(v);return 0;}
        } catch (...) {v.busy=false;}
        auto result=DefSubclassProc(w,msg,a,b);
        try {
            update(v);
            if(shift) extendFoldSelection(v);else clampFoldCaret(v);
            paintVisuals(v);
        } catch (...) {v.busy=false;}
        return result;
    }
    if(enabled && (msg==WM_LBUTTONDOWN || msg==WM_LBUTTONUP)) {
        try { update(v); } catch (...) { v.busy=false; }
        auto line=hit(v,GET_X_LPARAM(b),GET_Y_LPARAM(b));
        if(msg==WM_LBUTTONDOWN && line>=0) {
            v.pressed=line; SetFocus(w); SetCapture(w); return 0;
        }
        if(msg==WM_LBUTTONUP && v.pressed>=0) {
            auto pressed=v.pressed;v.pressed=-1;
            if(GetCapture()==w) ReleaseCapture();
            if(line==pressed && send(w,sci::GetDocPointer)==v.doc && !send(w,sci::GetFoldExpanded,line)) {
                send(w,sci::ToggleFold,line);
                send(v.tooltip,TTM_POP);
                clearVisuals(v);
                InvalidateRect(w,nullptr,FALSE);
            }
            return 0;
        }
        if(msg==WM_LBUTTONDOWN && line<0 && !shift && !control && !alt) {
            POINT point{GET_X_LPARAM(b),GET_Y_LPARAM(b)};
            for(const auto& entry:v.visuals) {
                const auto& visual=entry.second;
                if(visual.visible && PtInRect(&visual.mask,point)) {
                    SetFocus(w);
                    if(point.x>=visual.dots.right) placeCaretAfterFold(v,entry.first,visual);
                    else placeCaret(w,visual.selectionTrigger);
                    return 0;
                }
            }
        }
    }
    if(msg==WM_CAPTURECHANGED) v.pressed=-1;
    if(msg==WM_MOUSEMOVE && enabled && (GetCapture()!=w || v.pressed>=0) &&
        hit(v,GET_X_LPARAM(b),GET_Y_LPARAM(b))>=0) {
        // The ellipsis is our hit target, not editable text. Scintilla's normal
        // mouse-move handler would replace IDC_HAND with its text cursor.
        SetCursor(LoadCursor(nullptr,IDC_HAND));
        if(v.tooltip) {
            MSG event{};event.hwnd=w;event.message=msg;event.wParam=a;event.lParam=b;
            event.time=static_cast<DWORD>(GetMessageTime());GetCursorPos(&event.pt);
            send(v.tooltip,TTM_RELAYEVENT,0,reinterpret_cast<LPARAM>(&event));
        }
        return 0;
    }
    if(msg==WM_SETCURSOR && enabled) {
        POINT p{};GetCursorPos(&p);ScreenToClient(w,&p);
        if(hit(v,p.x,p.y)>=0) {SetCursor(LoadCursor(nullptr,IDC_HAND));return TRUE;}
    }
    if(msg==2233) { // SCI_SETFOLDFLAGS: remember host updates, suppress fold lines.
        if(!v.settingFoldFlags) v.originalFoldFlags=a;
        if(enabled && foldtail::supported(static_cast<int>(send(w,sci::GetLexer)))) a=0;
    }
    if(msg==WM_TIMER && a==v.timer) {
        // Never let allocation failures escape a Windows callback.
        try { update(v); } catch (...) { v.busy=false; send(w,WM_SETREDRAW,TRUE); }
        return 0;
    }
    if(msg==WM_NCDESTROY) {
        clearVisuals(v); if(v.tooltip) DestroyWindow(v.tooltip); v.tooltip=nullptr;
        KillTimer(w,v.timer); RemoveWindowSubclass(w,subclass,id); v.window=nullptr;
    }
    if(enabled && (msg==WM_LBUTTONUP || (msg==WM_MOUSEMOVE && (a&MK_LBUTTON)) ||
        (msg==WM_KEYUP && (a==VK_SHIFT || a==VK_LEFT || a==VK_RIGHT || a==VK_UP || a==VK_DOWN || a==VK_HOME || a==VK_END)))) {
        auto result=DefSubclassProc(w,msg,a,b);
        try {update(v);extendFoldSelection(v);paintVisuals(v);} catch (...) {v.busy=false;}
        return result;
    }
    return DefSubclassProc(w,msg,a,b);
}
static void start() {
    HWND handles[]={npp.main,npp.second};
    for(int i=0;i<2;++i) {
        auto& v=views[i];
        if(v.window || !IsWindow(handles[i])) continue;
        v.window=handles[i];
        const auto windowStyle=GetWindowLongPtr(v.window,GWL_STYLE);
        v.addedClipChildren=(windowStyle&WS_CLIPCHILDREN)==0;
        if(v.addedClipChildren) SetWindowLongPtr(v.window,GWL_STYLE,windowStyle|WS_CLIPCHILDREN);
        v.originalStyle=static_cast<int>(send(v.window,sci::FoldDisplayTextGetStyle));
        if(SetWindowSubclass(v.window,subclass,0x46544149,reinterpret_cast<DWORD_PTR>(&v))) {
            v.timer=SetTimer(v.window,0x46544149,200,nullptr);
            createTooltip(v);
            update(v);
        }
    }
}
static void toggle();
static void toggleFirstComment();
static void toggleLastComment();
static constexpr wchar_t projectUrl[]=L"https://github.com/seantw/NppFoldTail";
static HRESULT CALLBACK aboutCallback(HWND window,UINT notification,WPARAM,LPARAM,LONG_PTR) {
    if(notification==TDN_HYPERLINK_CLICKED) {
        const auto result=ShellExecuteW(window,L"open",projectUrl,nullptr,nullptr,SW_SHOWNORMAL);
        if(reinterpret_cast<INT_PTR>(result)<=32)
            MessageBoxW(window,L"Unable to open the browser.\n\nhttps://github.com/seantw/NppFoldTail",L"NppFoldTail",MB_OK|MB_ICONWARNING);
    }
    return S_OK;
}
static void about() {
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize=sizeof(dialog);
    dialog.hwndParent=npp.npp;
    dialog.dwFlags=TDF_ENABLE_HYPERLINKS|TDF_ALLOW_DIALOG_CANCELLATION|TDF_POSITION_RELATIVE_TO_WINDOW;
    dialog.dwCommonButtons=TDCBF_OK_BUTTON;
    dialog.pszWindowTitle=L"NppFoldTail";
    dialog.pszMainInstruction=L"NppFoldTail " NPPFOLDTAIL_VERSION_W L" (x64)";
    dialog.pszContent=L"A Notepad++ plugin for cleaner, more readable code folding.\n\n"
        L"<a href=\"https://github.com/seantw/NppFoldTail\">https://github.com/seantw/NppFoldTail</a>";
    dialog.pfCallback=aboutCallback;
    if(FAILED(TaskDialogIndirect(&dialog,nullptr,nullptr,nullptr)))
        MessageBoxW(npp.npp,L"NppFoldTail " NPPFOLDTAIL_VERSION_W L" (x64)\n\n"
            L"A Notepad++ plugin for cleaner, more readable code folding.\n\nhttps://github.com/seantw/NppFoldTail",L"NppFoldTail",MB_OK);
}
static FuncItem commands[]={
    {L"Enable",toggle,0,true,nullptr},
    {L"First Comment",toggleFirstComment,0,false,nullptr},
    {L"Last Comment",toggleLastComment,0,false,nullptr},
    {L"",nullptr,0,false,nullptr},
    {L"About NppFoldTail",about,0,false,nullptr}
};
static std::wstring settingsPath;
static void loadSettings() {
    constexpr UINT getConfigDir=WM_USER+1000+46; // NPPM_GETPLUGINSCONFIGDIR
    const auto length=send(npp.npp,getConfigDir);
    if(length<=0 || length>32767) return;
    std::vector<wchar_t> directory(static_cast<size_t>(length)+1,L'\0');
    if(!send(npp.npp,getConfigDir,directory.size(),reinterpret_cast<LPARAM>(directory.data()))) return;
    settingsPath=directory.data();
    if(settingsPath.empty()) return;
    CreateDirectoryW(settingsPath.c_str(),nullptr);
    if(settingsPath.back()!=L'\\') settingsPath+=L'\\';
    settingsPath+=L"NppFoldTail.ini";
    enabled=GetPrivateProfileIntW(L"Settings",L"Enable",1,settingsPath.c_str())!=0;
    showFirstComment=GetPrivateProfileIntW(L"Settings",L"FirstComment",0,settingsPath.c_str())!=0;
    showLastComment=GetPrivateProfileIntW(L"Settings",L"LastComment",0,settingsPath.c_str())!=0;
    commands[0].checked=enabled;
    commands[1].checked=showFirstComment;
    commands[2].checked=showLastComment;
    for(int i=0;i<3;++i)
        CheckMenuItem(GetMenu(npp.npp),commands[i].id,MF_BYCOMMAND|(commands[i].checked?MF_CHECKED:MF_UNCHECKED));
}
static void saveSettings() {
    bool saved=!settingsPath.empty();
    if(saved) {
        saved=WritePrivateProfileStringW(L"Settings",L"Enable",enabled?L"1":L"0",settingsPath.c_str())!=0;
        saved=(WritePrivateProfileStringW(L"Settings",L"FirstComment",showFirstComment?L"1":L"0",settingsPath.c_str())!=0) && saved;
        saved=(WritePrivateProfileStringW(L"Settings",L"LastComment",showLastComment?L"1":L"0",settingsPath.c_str())!=0) && saved;
    }
    if(!saved) MessageBoxW(npp.npp,L"Unable to save NppFoldTail settings. Check that the plugins config directory is writable.",L"NppFoldTail",MB_OK|MB_ICONWARNING);
}
static void toggleComment(bool& option,int index) {
    option=!option;
    commands[index].checked=option;
    saveSettings();
    CheckMenuItem(GetMenu(npp.npp),commands[index].id,MF_BYCOMMAND|(option?MF_CHECKED:MF_UNCHECKED));
    for(auto& v:views) if(IsWindow(v.window)) {
        clearVisuals(v);
        update(v);
        InvalidateRect(v.window,nullptr,FALSE);
    }
}
static void toggleFirstComment() {toggleComment(showFirstComment,1);}
static void toggleLastComment() {toggleComment(showLastComment,2);}
static void toggle() {
    enabled=!enabled;
    commands[0].checked=enabled;
    saveSettings();
    CheckMenuItem(GetMenu(npp.npp),commands[0].id,MF_BYCOMMAND|(enabled?MF_CHECKED:MF_UNCHECKED));
    for(auto& v:views) if(IsWindow(v.window)) {
        clearVisuals(v);
        refreshCssFolds(v.window);
        applyPresentation(v);
        InvalidateRect(v.window,nullptr,FALSE);
    }
}
extern "C" {
__declspec(dllexport) BOOL isUnicode() { return TRUE; }
__declspec(dllexport) const wchar_t* getName() { return L"NppFoldTail"; }
__declspec(dllexport) void setInfo(NppData data) { npp=data; }
__declspec(dllexport) FuncItem* getFuncsArray(int* n) { *n=static_cast<int>(sizeof(commands)/sizeof(commands[0])); return commands; }
// Only the common notification header is read; full SCNotification is not needed.
__declspec(dllexport) void beNotified(NMHDR* notification) {
    if(!notification) return;
    if(notification->code==2008 && !applyingCss) { // SCN_MODIFIED prefix from Scintilla.h.
        struct Modification {NMHDR header;LRESULT position;int ch,modifiers,modificationType;};
        const auto change=reinterpret_cast<const Modification*>(notification);
        if(change->modificationType&0xf) { // text, styles or native fold levels changed
            for(const auto& view:views) if(view.window==notification->hwndFrom) {
                cssDocuments[send(view.window,sci::GetDocPointer)].dirty=true;
                break;
            }
        }
    }
    if(notification->code==1001) {loadSettings();start();} // NPPN_READY
    if(notification->code==1009) { // NPPN_SHUTDOWN
        enabled=false;
        for(auto& v:views) if(IsWindow(v.window)) {
            refreshCssFolds(v.window);
            clearVisuals(v); if(v.tooltip) DestroyWindow(v.tooltip);v.tooltip=nullptr;
            KillTimer(v.window,v.timer);
            if(v.addedClipChildren) SetWindowLongPtr(v.window,GWL_STYLE,GetWindowLongPtr(v.window,GWL_STYLE)&~static_cast<LONG_PTR>(WS_CLIPCHILDREN));
            RemoveWindowSubclass(v.window,subclass,0x46544149);
        }
    }
}
__declspec(dllexport) LRESULT messageProc(UINT,WPARAM,LPARAM) { return TRUE; }
}
