#pragma once
// Included after the plugin's View, label() and send() declarations.
// This layer paints only: it never changes document text or syntax styles.
static std::wstring wide(const std::string& text, UINT cp) {
    if(text.empty()) return {};
    int n=MultiByteToWideChar(cp,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(n<=0) return {};
    std::wstring result(n,L'\0');
    MultiByteToWideChar(cp,0,text.data(),static_cast<int>(text.size()),result.data(),n);
    return result;
}
static TOOLINFOW tool(View& v,LRESULT line) {
    TOOLINFOW ti{};ti.cbSize=TTTOOLINFOW_V2_SIZE;ti.hwnd=v.window;
    ti.uId=static_cast<UINT_PTR>(line+1);ti.uFlags=TTF_SUBCLASS;
    return ti;
}
static void createTooltip(View& v) {
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_WIN95_CLASSES};InitCommonControlsEx(&cc);
    v.tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,
        WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,CW_USEDEFAULT,CW_USEDEFAULT,
        CW_USEDEFAULT,CW_USEDEFAULT,v.window,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(v.tooltip) {
        send(v.tooltip,TTM_SETMAXTIPWIDTH,0,1000);
        send(v.tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,500);
        send(v.tooltip,TTM_SETDELAYTIME,TTDT_AUTOPOP,20000);
    }
}
static void clearVisuals(View& v) {
    if(v.tooltip) {
        send(v.tooltip,TTM_POP);
        for(const auto& entry:v.visuals) {
            auto ti=tool(v,entry.first);send(v.tooltip,TTM_DELTOOLW,0,reinterpret_cast<LPARAM>(&ti));
        }
    }
    v.visuals.clear();v.pressed=-1;
}
static HFONT visualFont(HWND w,int style,HDC dc) {
    char face[256]{};
    send(w,2486,style,reinterpret_cast<LPARAM>(face)); // SCI_STYLEGETFONT
    auto name=wide(face,CP_UTF8);
    auto size=send(w,2062,style); // SCI_STYLEGETSIZEFRACTIONAL (hundredths of a point)
    if(size<=0) size=1000;
    size+=100*send(w,2374); // SCI_GETZOOM
    auto height=std::max(1,MulDiv(static_cast<int>(size),GetDeviceCaps(dc,LOGPIXELSY),7200));
    return CreateFontW(-height,0,0,0,send(w,2483,style)?FW_BOLD:FW_NORMAL,
        static_cast<DWORD>(send(w,2484,style)),FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,
        name.empty()?L"Consolas":name.c_str());
}
static bool sameRect(const RECT& a,const RECT& b) {return EqualRect(&a,&b)!=FALSE;}
static LRESULT hit(const View& v,int x,int y) {
    POINT p{x,y};
    for(const auto& entry:v.visuals) if(entry.second.visible && PtInRect(&entry.second.dots,p)) return entry.first;
    return -1;
}
static LRESULT headerCut(HWND w,LRESULT line,LRESULT last,const std::string& text,int lexer) {
    const auto pos=send(w,sci::PositionFromLine,line);
    auto cut=send(w,2136,line); // SCI_GETLINEENDPOSITION
    for(size_t j=0;j<text.size();++j) {
        if((text[j]!='{' && !(lexer==120 && text[j]=='[')) ||
            syntaxStyle(w,pos+j,lexer)!=foldtail::operatorStyle(lexer)) continue;
        auto match=send(w,2353,pos+j,0); // SCI_BRACEMATCH
        if(lexer==foldtail::htmlCss) {
            const auto& braces=cssDocuments.at(send(w,sci::GetDocPointer)).braces;
            const auto found=braces.find(pos+j);
            match=found==braces.end()?-1:found->second;
        }
        const auto closeLine=match>=0 ? send(w,2166,match) : -1;
        if(closeLine>line && closeLine<=last) {cut=pos+j+1;break;}
    }
    return cut;
}
static LRESULT commentCut(HWND w,LRESULT header,LRESULT last,const std::string& text,int lexer) {
    const auto start=text.find_first_not_of(" \t");
    if(start==std::string::npos || text.compare(start,2,"/*")!=0 || text.find("*/",start+2)!=std::string::npos) return -1;
    const auto pos=send(w,sci::PositionFromLine,header);
    if(!foldtail::blockComment(lexer,syntaxStyle(w,pos+start,lexer))) return -1;
    for(auto line=last;line>header && line>=last-8;--line) {
        if(send(w,sci::GetLineVisible,line)) continue;
        const auto endText=readLine(w,line);
        const auto end=endText.rfind("*/");
        if(end==std::string::npos) continue;
        const auto endPos=send(w,sci::PositionFromLine,line)+end;
        if(foldtail::blockComment(lexer,syntaxStyle(w,endPos,lexer))) return pos+start+2;
    }
    return -1;
}
static std::wstring blockTooltip(HWND w,LRESULT header,LRESULT last,UINT cp) {
    std::vector<std::wstring> lines;
    for(auto line=header;line<=last && line<header+10;++line) lines.push_back(wide(readLine(w,line),cp));
    return foldtail::preview(lines,last>=header+10);
}
struct TrailingComment {LRESULT position=-1;std::string text;};
static TrailingComment trailingComment(HWND w,LRESULT line,int lexer,LRESULT after) {
    if(line<0) return {};
    const auto text=readLine(w,line);
    const auto pos=send(w,sci::PositionFromLine,line);
    bool code=false;
    for(size_t i=0;i<text.size();++i) {
        const bool isComment=foldtail::comment(lexer,syntaxStyle(w,pos+i,lexer));
        if(isComment && code && pos+static_cast<LRESULT>(i)>=after) {
            auto end=i;
            while(end<text.size() && foldtail::comment(lexer,syntaxStyle(w,pos+end,lexer))) ++end;
            while(end>i && std::isspace(static_cast<unsigned char>(text[end-1]))) --end;
            return {pos+static_cast<LRESULT>(i),text.substr(i,end-i)};
        }
        if(!isComment && !std::isspace(static_cast<unsigned char>(text[i]))) code=true;
    }
    return {};
}
struct HtmlInfo {LRESULT cut=-1;std::string suffix;LRESULT endLine=-1;};
static HtmlInfo htmlInfo(HWND w,LRESULT header,LRESULT last,const std::string& text,bool xml) {
    const auto pos=send(w,sci::PositionFromLine,header);
    const auto comment=text.find_first_not_of(" \t");
    if(comment!=std::string::npos && text.compare(comment,4,"<!--")==0 &&
        send(w,sci::GetStyleAt,pos+comment)==9) { // SCE_H_COMMENT
        if(text.find("-->",comment+4)!=std::string::npos) return {};
        for(auto line=last;line>header && line>=last-8;--line) {
            if(send(w,sci::GetLineVisible,line)) continue;
            const auto endText=readLine(w,line);
            const auto end=endText.find("-->");
            if(end==std::string::npos) continue;
            const auto endPos=send(w,sci::PositionFromLine,line)+end;
            if(send(w,sci::GetStyleAt,endPos)==9)
                return {pos+static_cast<LRESULT>(comment)+4,"-->",line};
        }
        return {};
    }
    const auto opening=foldtail::htmlStart(text,[&](size_t i){return send(w,sci::GetStyleAt,pos+i);},xml);
    if(opening.end==std::string::npos) return {};
    for(auto line=last;line>header && line>=last-8;--line) {
        if(send(w,sci::GetLineVisible,line)) continue;
        const auto lastText=readLine(w,line);
        const auto lastPos=send(w,sci::PositionFromLine,line);
        auto suffix=foldtail::htmlTail(lastText,opening.name,[&](size_t i){return send(w,sci::GetStyleAt,lastPos+i);},xml);
        if(!suffix.empty()) return {pos+static_cast<LRESULT>(opening.end),std::move(suffix),line};
    }
    return {};
}
static void update(View& v,bool includeHidden=false) {
    if(v.busy || applyingCss) return;
    const auto document=send(v.window,sci::GetDocPointer);
    if(document!=v.doc) cssDocuments[document].dirty=true;
    refreshCssFolds(v.window);
    if(!applyPresentation(v)) {
        if(!v.visuals.empty()) {clearVisuals(v);InvalidateRect(v.window,nullptr,FALSE);}
        return;
    }
    if(!includeHidden && !IsWindowVisible(v.window)) return;
    v.busy=true;
    auto w=v.window;
    const auto doc=send(w,sci::GetDocPointer);
    if(doc!=v.doc) {clearVisuals(v);v.doc=doc;}
    RECT client{};GetClientRect(w,&client);
    HDC dc=GetDC(w);
    if(!dc) {v.busy=false;return;}
    std::map<LRESULT,Visual> next;
    const auto first=send(w,sci::GetFirstVisibleLine);
    const auto count=std::min<LRESULT>(send(w,sci::LinesOnScreen)+2,2000);
    const auto total=send(w,sci::GetLineCount);
    const auto lexer=static_cast<int>(send(w,sci::GetLexer));
    const auto cp=static_cast<UINT>(send(w,2137)); // SCI_GETCODEPAGE
    if(foldtail::supported(lexer)) for(LRESULT i=0;i<count;++i) {
        const auto line=send(w,sci::DocLineFromVisible,first+i);
        if(line<0 || line>=total) break;
        if(next.count(line) || !(send(w,sci::GetFoldLevel,line)&0x2000) || send(w,sci::GetFoldExpanded,line)) continue;
        // A paint overlay cannot remove the extra display rows of a wrapped header.
        if(send(w,2235,line)>1) continue; // SCI_WRAPCOUNT
        const auto firstText=readLine(w,line);
        const auto pos=send(w,sci::PositionFromLine,line);
        const auto last=send(w,sci::GetLastChild,line,-1);
        const auto firstNonSpace=firstText.find_first_not_of(" \t\r\n");
        const auto headerStyle=firstNonSpace==std::string::npos?0:send(w,sci::GetStyleAt,pos+firstNonSpace);
        const bool phpCode=(lexer==4 || lexer==69) &&
            ((headerStyle>=118 && headerStyle<=127) ||
             (firstNonSpace!=std::string::npos && firstText.compare(firstNonSpace,5,"<?php")==0));
        const bool htmlLexer=lexer==4 || lexer==5 || lexer==69;
        const bool jsCode=htmlLexer && headerStyle>=40 && headerStyle<=53;
        const bool aspJsCode=htmlLexer && headerStyle>=55 && headerStyle<=68;
        const bool cssCode=firstNonSpace!=std::string::npos && syntaxStyle(w,pos+firstNonSpace,foldtail::htmlCss)!=0;
        const auto codeLexer=cssCode?foldtail::htmlCss:phpCode?69:jsCode?foldtail::htmlJavaScript:
            aspJsCode?foldtail::htmlAspJavaScript:lexer;
        const bool isIndentBased=lexer==2 || lexer==48; // Python and YAML folds have no closing suffix.
        const bool isHtml=(lexer==4 || lexer==5) && !phpCode && !jsCode && !aspJsCode && !cssCode;
        const auto html=isHtml?htmlInfo(w,line,last,firstText,lexer==5):HtmlInfo{};
        if(isHtml && html.cut<0) continue;
        const auto commentPos=isHtml || isIndentBased?-1:commentCut(w,line,last,firstText,codeLexer);
        auto cut=isIndentBased?send(w,2136,line):isHtml?html.cut:commentPos>=0?commentPos:headerCut(w,line,last,firstText,codeLexer);
        const auto tail=isIndentBased?TailInfo{"",readLine(w,last)}:commentPos>=0 || isHtml?TailInfo{}:label(w,line,codeLexer);
        auto suffix=isHtml?html.suffix:commentPos>=0?std::string("*/"):tail.suffix;
        if(suffix.empty() && !isIndentBased) continue;
        const bool commentBlock=commentPos>=0 || (isHtml && html.suffix=="-->");
        const auto firstComment=commentBlock?TrailingComment{}:trailingComment(w,line,codeLexer,isIndentBased?pos:cut);
        const auto endLine=isHtml?html.endLine:isIndentBased?last:tail.line;
        const auto lastComment=commentBlock?TrailingComment{}:trailingComment(w,endLine,codeLexer,0);
        if(isIndentBased && firstComment.position>=0) {
            cut=firstComment.position;
            while(cut>pos && (firstText[static_cast<size_t>(cut-pos-1)]==' ' || firstText[static_cast<size_t>(cut-pos-1)]=='\t')) --cut;
        }
        if(isHtml && !lastComment.text.empty()) {
            const auto commentOffset=suffix.rfind(lastComment.text);
            if(commentOffset!=std::string::npos) {
                suffix.erase(commentOffset);
                while(!suffix.empty() && std::isspace(static_cast<unsigned char>(suffix.back()))) suffix.pop_back();
            }
        }
        Visual visual{};visual.doc=doc;
        visual.selectionTrigger=cut;
        visual.blockEnd=send(w,2136,last); // Include the complete closing line, but not the following newline.
        visual.x=static_cast<int>(send(w,2164,0,cut));
        visual.y=static_cast<int>(send(w,2165,0,cut));
        visual.height=static_cast<int>(send(w,2279,line)); // SCI_TEXTHEIGHT
        visual.style=static_cast<int>(send(w,sci::GetStyleAt,cut>pos?cut-1:pos));
        visual.text=L" \u2026 "+wide(suffix,cp);
        visual.commentStart=visual.text.size();
        visual.commentStyle=foldtail::singleLineCommentStyle(codeLexer);
        const auto firstShown=showFirstComment?firstComment.text:std::string{};
        const auto lastShown=showLastComment?lastComment.text:std::string{};
        if(!firstShown.empty()) visual.text+=L" "+wide(firstShown,cp);
        if(!lastShown.empty()) visual.text+=(firstShown.empty()?L" ":L" \uFF5E ")+wide(lastShown,cp);
        visual.tip=blockTooltip(w,line,last,cp);
        visual.mask={visual.x,visual.y,client.right,visual.y+visual.height};
        // Never paint over the margin when a header is horizontally out of view.
        const auto textStart=send(w,2164,0,pos)+send(w,2398); // SCI_GETXOFFSET
        if(visual.x<textStart || visual.x<0 || visual.x>=client.right ||
            visual.y+visual.height<=0 || visual.y>=client.bottom) continue;
        HFONT font=visualFont(w,visual.style,dc);
        HGDIOBJ old=font?SelectObject(dc,font):nullptr;
        SIZE space{},dots{};
        GetTextExtentPoint32W(dc,L" ",1,&space);GetTextExtentPoint32W(dc,L"\u2026",1,&dots);
        visual.dots={visual.x+space.cx,visual.y,visual.x+space.cx+dots.cx,visual.y+visual.height};
        if(old) SelectObject(dc,old);
        if(font) DeleteObject(font);
        next.emplace(line,std::move(visual));
    }
    ReleaseDC(w,dc);
    for(auto it=v.visuals.begin();it!=v.visuals.end();) {
        if(!next.count(it->first)) {
            if(it->second.visible) {
                auto ti=tool(v,it->first);send(v.tooltip,TTM_DELTOOLW,0,reinterpret_cast<LPARAM>(&ti));
                it->second.visible=false;
                if(it->second.cache && it->second.cache->overlay) ShowWindow(it->second.cache->overlay,SW_HIDE);
                // Hiding the child invalidates the exposed parent area itself.
            }
            if(it->first>=total || send(w,sci::GetFoldExpanded,it->first)) it=v.visuals.erase(it);
            else ++it;
        } else ++it;
    }
    for(auto& entry:next) {
        auto old=v.visuals.find(entry.first);
        const bool added=old==v.visuals.end();
        const bool wasVisible=!added && old->second.visible;
        const bool sameImage=!added && old->second.text==entry.second.text &&
            old->second.commentStart==entry.second.commentStart && old->second.commentStyle==entry.second.commentStyle &&
            old->second.style==entry.second.style && old->second.height==entry.second.height;
        const bool sameTip=!added && old->second.tip==entry.second.tip;
        const bool samePosition=!added && sameRect(old->second.mask,entry.second.mask) &&
            sameRect(old->second.dots,entry.second.dots);
        if(wasVisible && sameImage && sameTip && samePosition &&
            old->second.selectionTrigger==entry.second.selectionTrigger && old->second.blockEnd==entry.second.blockEnd) continue;
        if(!added && sameImage && sameTip) {
            // Scrolling changes geometry only. Keep both string buffers, the
            // bitmap, child HWND and tooltip registration alive.
            auto& saved=old->second;
            saved.visible=true;
            saved.x=entry.second.x;saved.y=entry.second.y;
            saved.mask=entry.second.mask;saved.dots=entry.second.dots;
            saved.selectionTrigger=entry.second.selectionTrigger;saved.blockEnd=entry.second.blockEnd;
            auto ti=tool(v,entry.first);ti.rect=saved.dots;ti.lpszText=saved.tip.data();
            send(v.tooltip,wasVisible?TTM_NEWTOOLRECTW:TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&ti));
            if(!saved.cache) InvalidateRect(w,&saved.mask,FALSE);
            continue;
        }
        if(wasVisible) {
            if(!sameTip || !samePosition) {
                auto ti=tool(v,entry.first);send(v.tooltip,TTM_DELTOOLW,0,reinterpret_cast<LPARAM>(&ti));
            }
        }
        if(sameImage) entry.second.cache=std::move(old->second.cache);
        auto& saved=v.visuals[entry.first];saved=std::move(entry.second);
        auto ti=tool(v,entry.first);ti.rect=saved.dots;ti.lpszText=saved.tip.data();
        // The string buffer may move even if its text is identical.
        if(wasVisible && sameTip && samePosition)
            send(v.tooltip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&ti));
        else send(v.tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&ti));
        if(!sameImage || !wasVisible) InvalidateRect(w,&saved.mask,FALSE);
    }
    // Retain a bounded off-screen cache rather than growing with the document.
    for(auto it=v.visuals.begin();v.visuals.size()>256 && it!=v.visuals.end();) {
        if(!it->second.visible) it=v.visuals.erase(it); else ++it;
    }
    v.busy=false;
}
static LRESULT CALLBACK previewWindowProc(HWND window,UINT message,WPARAM a,LPARAM b) {
    if(message==WM_NCCREATE) {
        const auto create=reinterpret_cast<CREATESTRUCTW*>(b);
        SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if(message==WM_NCHITTEST) return HTTRANSPARENT;
    if(message==WM_ERASEBKGND) return 1;
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);
        const auto cache=reinterpret_cast<PreviewBitmap*>(GetWindowLongPtr(window,GWLP_USERDATA));
        if(cache && cache->bitmap) {
            HDC source=CreateCompatibleDC(dc);
            if(source) {
                auto old=SelectObject(source,cache->bitmap);
                BitBlt(dc,0,0,cache->width,cache->height,source,0,0,SRCCOPY);
                SelectObject(source,old);DeleteDC(source);
            }
        }
        EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,a,b);
}
static void paintVisuals(View& v) {
    if(!enabled || v.visuals.empty() || send(v.window,sci::GetDocPointer)!=v.doc) return;
    HDC dc=GetDC(v.window);if(!dc) return;
    HDC buffer=CreateCompatibleDC(dc);
    if(!buffer) {ReleaseDC(v.window,dc);return;}
    RECT client{};GetClientRect(v.window,&client);
    HDWP positions=BeginDeferWindowPos(static_cast<int>(v.visuals.size()));
    for(auto& entry:v.visuals) {
        auto& visual=entry.second;
        if(!visual.visible) continue;
        if(send(v.window,sci::GetFoldExpanded,entry.first)) continue;
        HFONT font=visualFont(v.window,visual.style,dc);
        if(!font) continue;
        LOGFONTW fontInfo{};
        GetObjectW(font,sizeof(fontInfo),&fontInfo);
        auto foreground=static_cast<COLORREF>(send(v.window,2481,visual.style));
        auto commentColour=static_cast<COLORREF>(send(v.window,2481,visual.commentStyle));
        const auto normalBackground=static_cast<COLORREF>(send(v.window,2482,visual.style));
        auto background=normalBackground;
        // STYLE_BRACELIGHT (34) follows the active Notepad++ theme settings.
        auto markerForeground=static_cast<COLORREF>(send(v.window,2481,34));
        auto markerBackground=static_cast<COLORREF>(send(v.window,2482,34));
        const auto selectionStart=send(v.window,2143),selectionEnd=send(v.window,2145);
        const bool selected=selectionStart<visual.selectionTrigger && selectionEnd>=visual.blockEnd;
        if(selected) {
            const int backElement=GetFocus()==v.window?11:17;
            const int textElement=GetFocus()==v.window?10:16;
            const auto colour=static_cast<DWORD>(send(v.window,2754,backElement));
            const auto alpha=(colour>>24)&255;
            const auto blend=[&](DWORD channel,DWORD base) {return (channel*alpha+base*(255-alpha)+127)/255;};
            background=RGB(blend(GetRValue(colour),GetRValue(normalBackground)),
                blend(GetGValue(colour),GetGValue(normalBackground)),blend(GetBValue(colour),GetBValue(normalBackground)));
            if(send(v.window,2756,textElement)) foreground=commentColour=static_cast<COLORREF>(send(v.window,2754,textElement))&0xffffff;
            markerBackground=background;
            if(send(v.window,2756,textElement)) markerForeground=foreground;
        }
        const int overlayWidth=visual.mask.right-visual.mask.left;
        const int width=client.right; // Horizontal scrolling changes clipping, not bitmap content.
        const int height=visual.height;
        if(width<=0 || overlayWidth<=0 || height<=0) {DeleteObject(font);continue;}
        auto& cache=visual.cache;
        // Position changes retain the image; only content/appearance/viewport size rebuild it.
        if(cache && (cache->width!=width || cache->height!=height ||
            cache->foreground!=foreground || cache->commentColour!=commentColour ||
            cache->markerForeground!=markerForeground || cache->markerBackground!=markerBackground ||
            cache->background!=background || std::memcmp(&cache->font,&fontInfo,sizeof(fontInfo))!=0)) cache.reset();
        if(!cache) {
            auto next=std::make_unique<PreviewBitmap>();
            next->bitmap=CreateCompatibleBitmap(dc,width,height);
            if(!next->bitmap) {DeleteObject(font);continue;}
            next->width=width;next->height=height;next->font=fontInfo;
            next->foreground=foreground;next->commentColour=commentColour;next->background=background;
            next->markerForeground=markerForeground;next->markerBackground=markerBackground;
            auto oldBitmap=SelectObject(buffer,next->bitmap);
            auto oldFont=SelectObject(buffer,font);
            SetTextColor(buffer,foreground);SetBkColor(buffer,background);
            TEXTMETRICW metrics{};GetTextMetricsW(buffer,&metrics);
            const auto y=std::max<LONG>(0,(height-metrics.tmHeight)/2);
            RECT mask{0,0,width,height};
            SetBkColor(buffer,normalBackground);
            ExtTextOutW(buffer,0,0,ETO_OPAQUE,&mask,nullptr,0,nullptr);
            SIZE textSize{};GetTextExtentPoint32W(buffer,visual.text.data(),static_cast<int>(visual.text.size()),&textSize);
            RECT textMask{0,0,std::min<LONG>(width,textSize.cx),height};
            SetBkColor(buffer,background);
            ExtTextOutW(buffer,0,y,ETO_OPAQUE|ETO_CLIPPED,&textMask,visual.text.data(),static_cast<UINT>(visual.commentStart),nullptr);
            const auto markerLength=std::min<size_t>(3,visual.commentStart);
            SIZE markerSize{};GetTextExtentPoint32W(buffer,visual.text.data(),static_cast<int>(markerLength),&markerSize);
            RECT markerMask{0,0,std::min<LONG>(width,markerSize.cx),height};
            SetTextColor(buffer,markerForeground);SetBkColor(buffer,markerBackground);
            ExtTextOutW(buffer,0,y,ETO_OPAQUE|ETO_CLIPPED,&markerMask,visual.text.data(),static_cast<UINT>(markerLength),nullptr);
            if(visual.commentStart<visual.text.size()) {
                SIZE prefix{};
                GetTextExtentPoint32W(buffer,visual.text.data(),static_cast<int>(visual.commentStart),&prefix);
                SetTextColor(buffer,commentColour);
                const auto oldMode=SetBkMode(buffer,TRANSPARENT);
                ExtTextOutW(buffer,prefix.cx,y,ETO_CLIPPED,&mask,visual.text.data()+visual.commentStart,
                    static_cast<UINT>(visual.text.size()-visual.commentStart),nullptr);
                SetBkMode(buffer,oldMode);
            }
            SelectObject(buffer,oldFont);SelectObject(buffer,oldBitmap);
            cache=std::move(next);
        }
        // A child window owns these pixels; editor caret paints cannot erase them.
        if(!cache->overlay) {
            static const auto registered=[] {
                WNDCLASSW cls{};cls.lpfnWndProc=previewWindowProc;
                cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"NppFoldTailPreview";
                return RegisterClassW(&cls);
            }();
            if(registered) cache->overlay=CreateWindowExW(WS_EX_NOACTIVATE,L"NppFoldTailPreview",L"",
                WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,visual.x,visual.y,overlayWidth,height,
                v.window,nullptr,GetModuleHandleW(nullptr),cache.get());
        }
        if(cache->overlay) {
            RECT actual{};GetWindowRect(cache->overlay,&actual);
            MapWindowPoints(nullptr,v.window,reinterpret_cast<POINT*>(&actual),2);
            if(!sameRect(actual,visual.mask) || !IsWindowVisible(cache->overlay)) {
                constexpr UINT flags=SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_SHOWWINDOW;
                if(positions) positions=DeferWindowPos(positions,cache->overlay,nullptr,
                    visual.x,visual.y,overlayWidth,height,flags);
                else SetWindowPos(cache->overlay,nullptr,visual.x,visual.y,overlayWidth,height,flags);
            }
        }
        DeleteObject(font);
    }
    if(positions) EndDeferWindowPos(positions);
    DeleteDC(buffer);
    ReleaseDC(v.window,dc);
}
