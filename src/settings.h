#pragma once
struct DialogOptions {
    bool enable=true,first=false,last=false,css=true,spacing=false;
    int above=0,below=0;
    static DialogOptions current() {
        return {enabled,showFirstComment,showLastComment,cssFolding,customLineSpacing,extraAscent,extraDescent};
    }
    void activate() const {
        enabled=enable;showFirstComment=first;showLastComment=last;
        cssFolding=css;customLineSpacing=spacing;extraAscent=above;extraDescent=below;
    }
};
struct SettingsDialogState {bool filling=false;};
static void spacingControls(HWND dialog) {
    const bool custom=IsDlgButtonChecked(dialog,IDC_CUSTOM_SPACING)==BST_CHECKED;
    for(const auto id:{IDC_ASCENT,IDC_DESCENT,IDC_ASCENT_SPIN,IDC_DESCENT_SPIN})
        EnableWindow(GetDlgItem(dialog,id),custom);
}
static void fillSettings(HWND dialog,const DialogOptions& options,SettingsDialogState& state) {
    state.filling=true;
    CheckDlgButton(dialog,IDC_ENABLE,options.enable?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(dialog,IDC_FIRST_COMMENT,options.first?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(dialog,IDC_LAST_COMMENT,options.last?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(dialog,IDC_CSS_FOLDING,options.css?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(dialog,IDC_CUSTOM_SPACING,options.spacing?BST_CHECKED:BST_UNCHECKED);
    SetDlgItemInt(dialog,IDC_ASCENT,options.above,FALSE);
    SetDlgItemInt(dialog,IDC_DESCENT,options.below,FALSE);
    spacingControls(dialog);
    state.filling=false;
}
static bool applyDialogSettings(HWND dialog) {
    const auto previous=DialogOptions::current();
    auto options=previous;
    options.enable=IsDlgButtonChecked(dialog,IDC_ENABLE)==BST_CHECKED;
    options.first=IsDlgButtonChecked(dialog,IDC_FIRST_COMMENT)==BST_CHECKED;
    options.last=IsDlgButtonChecked(dialog,IDC_LAST_COMMENT)==BST_CHECKED;
    options.css=IsDlgButtonChecked(dialog,IDC_CSS_FOLDING)==BST_CHECKED;
    options.spacing=IsDlgButtonChecked(dialog,IDC_CUSTOM_SPACING)==BST_CHECKED;
    for(const auto id:{IDC_ASCENT,IDC_DESCENT}) {
        BOOL valid=FALSE;
        const auto value=GetDlgItemInt(dialog,id,&valid,FALSE);
        if(!valid || value>20) {
            if(!options.spacing) continue;
            MessageBoxW(dialog,L"Enter a whole number from 0 to 20 pixels for each side.",L"NppFoldTail",MB_OK|MB_ICONWARNING);
            SetFocus(GetDlgItem(dialog,id));
            SendDlgItemMessageW(dialog,id,EM_SETSEL,0,-1);
            return false;
        }
        if(id==IDC_ASCENT) options.above=static_cast<int>(value);
        else options.below=static_cast<int>(value);
    }
    options.activate();
    if(!saveSettings()) {previous.activate();return false;}
    applySettings();
    EnableWindow(GetDlgItem(dialog,IDC_APPLY),FALSE);
    return true;
}
static INT_PTR CALLBACK settingsDialogProc(HWND dialog,UINT message,WPARAM a,LPARAM b) {
    try {
        auto state=reinterpret_cast<SettingsDialogState*>(GetWindowLongPtrW(dialog,DWLP_USER));
        if(message==WM_INITDIALOG) {
            state=reinterpret_cast<SettingsDialogState*>(b);
            SetWindowLongPtrW(dialog,DWLP_USER,reinterpret_cast<LONG_PTR>(state));
            SendDlgItemMessageW(dialog,IDC_ASCENT_SPIN,UDM_SETBUDDY,reinterpret_cast<WPARAM>(GetDlgItem(dialog,IDC_ASCENT)),0);
            SendDlgItemMessageW(dialog,IDC_DESCENT_SPIN,UDM_SETBUDDY,reinterpret_cast<WPARAM>(GetDlgItem(dialog,IDC_DESCENT)),0);
            SendDlgItemMessageW(dialog,IDC_ASCENT_SPIN,UDM_SETRANGE32,0,20);
            SendDlgItemMessageW(dialog,IDC_DESCENT_SPIN,UDM_SETRANGE32,0,20);
            SendDlgItemMessageW(dialog,IDC_ASCENT,EM_SETLIMITTEXT,2,0);
            SendDlgItemMessageW(dialog,IDC_DESCENT,EM_SETLIMITTEXT,2,0);
            fillSettings(dialog,DialogOptions::current(),*state);
            EnableWindow(GetDlgItem(dialog,IDC_APPLY),FALSE);
            RECT owner{},rect{};
            GetWindowRect(npp.npp,&owner);GetWindowRect(dialog,&rect);
            MONITORINFO monitor{sizeof(monitor),{},{},0};
            GetMonitorInfoW(MonitorFromWindow(npp.npp,MONITOR_DEFAULTTONEAREST),&monitor);
            const auto width=rect.right-rect.left,height=rect.bottom-rect.top;
            const auto x=std::clamp<LONG>(owner.left+(owner.right-owner.left-width)/2,
                monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-width));
            const auto y=std::clamp<LONG>(owner.top+(owner.bottom-owner.top-height)/2,
                monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-height));
            SetWindowPos(dialog,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            return TRUE;
        }
        if(message==WM_CLOSE) {EndDialog(dialog,IDCANCEL);return TRUE;}
        if(message==WM_COMMAND && state) {
            const auto id=LOWORD(a);
            if(id==IDCANCEL) {EndDialog(dialog,IDCANCEL);return TRUE;}
            if(id==IDOK || id==IDC_APPLY) {
                if(applyDialogSettings(dialog) && id==IDOK) EndDialog(dialog,IDOK);
                return TRUE;
            }
            if(id==IDC_DEFAULTS && HIWORD(a)==BN_CLICKED) {
                fillSettings(dialog,DialogOptions{},*state);
                EnableWindow(GetDlgItem(dialog,IDC_APPLY),TRUE);
                return TRUE;
            }
            const bool check=id==IDC_ENABLE || id==IDC_FIRST_COMMENT || id==IDC_LAST_COMMENT ||
                id==IDC_CSS_FOLDING || id==IDC_CUSTOM_SPACING;
            const bool edit=id==IDC_ASCENT || id==IDC_DESCENT;
            if(!state->filling && ((check && HIWORD(a)==BN_CLICKED) || (edit && HIWORD(a)==EN_CHANGE))) {
                spacingControls(dialog);
                EnableWindow(GetDlgItem(dialog,IDC_APPLY),TRUE);
                return TRUE;
            }
        }
    } catch(...) {
        MessageBoxW(dialog,L"Unable to apply NppFoldTail settings.",L"NppFoldTail",MB_OK|MB_ICONWARNING);
    }
    return FALSE;
}
static void showSettings() {
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&showSettings),&module)) return;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&controls);
    SettingsDialogState state;
    if(DialogBoxParamW(module,MAKEINTRESOURCEW(IDD_SETTINGS),npp.npp,settingsDialogProc,
        reinterpret_cast<LPARAM>(&state))==-1)
        MessageBoxW(npp.npp,L"Unable to open NppFoldTail settings.",L"NppFoldTail",MB_OK|MB_ICONWARNING);
}
