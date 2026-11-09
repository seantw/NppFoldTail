#pragma once
struct MarkdownDocument {
    LRESULT doc{};
    bool dirty=true,active=false;
    LRESULT firstHeader=-1;
    int firstLevel=0;
};
static std::map<LRESULT,MarkdownDocument> markdownDocuments;

static void identifyMarkdown(View& v,LRESULT doc) {
    const auto now=GetTickCount64();
    if(v.markdownDoc==doc && now-v.markdownPathChecked<250) return;
    constexpr UINT nppMessage=WM_USER+1000;
    const int pane=v.window==npp.second?1:0;
    const auto index=send(npp.npp,nppMessage+23,0,pane);
    if(index<0) {
        if(v.markdownDoc!=doc) {v.markdownBuffer=0;v.markdownFile=false;}
        return; // Keep cached identity for a temporarily hidden view.
    }
    v.markdownDoc=doc;
    v.markdownPathChecked=now;
    v.markdownBuffer=send(npp.npp,nppMessage+59,index,pane);
    v.markdownFile=false;
    if(!v.markdownBuffer) return;
    const auto length=send(npp.npp,nppMessage+58,v.markdownBuffer);
    if(length<=0 || length>32767) return;
    std::wstring path(static_cast<size_t>(length)+1,L'\0');
    if(send(npp.npp,nppMessage+58,v.markdownBuffer,reinterpret_cast<LPARAM>(path.data()))<0) return;
    path.resize(static_cast<size_t>(length));
    const auto dot=path.find_last_of(L".\\/");
    if(dot!=std::wstring::npos)
        v.markdownFile=_wcsicmp(path.c_str()+dot,L".md")==0 || _wcsicmp(path.c_str()+dot,L".markdown")==0;
}

struct MarkdownScanner {
    char fence=0;
    size_t fenceLength=0;
    int heading(std::string_view text) {
        const auto start=text.find_first_not_of(' ');
        if(start==std::string_view::npos || start>3) return 0;
        text.remove_prefix(start);
        const auto end=text.find_last_not_of(" \t\r\n");
        if(end==std::string_view::npos) return 0;
        text=text.substr(0,end+1);
        const auto first=text.front();
        if(first=='`' || first=='~') {
            size_t count=1;
            while(count<text.size() && text[count]==first) ++count;
            if(fence && first==fence && count>=fenceLength && count==text.size()) fence=0;
            else if(!fence && count>=3 && (first!='`' || text.find('`',count)==std::string_view::npos)) {
                fence=first;fenceLength=count;
            }
            return 0;
        }
        if(fence || first!='#') return 0;
        size_t count=1;
        while(count<text.size() && text[count]=='#') ++count;
        return count<=6 && (count==text.size() || text[count]==' ' || text[count]=='\t')?
            static_cast<int>(count):0;
    }
};

static void refreshMarkdownFolds(View& v) {
    const auto w=v.window;
    const auto doc=send(w,sci::GetDocPointer);
    identifyMarkdown(v,doc);
    const auto lexer=static_cast<int>(send(w,sci::GetLexer));
    const bool active=enabled && markdownFolding && v.markdownFile && !foldtail::supported(lexer);
    v.markdownActive=active;
    if(!v.markdownBuffer) return;
    auto it=markdownDocuments.find(v.markdownBuffer);
    if(it==markdownDocuments.end()) {
        if(!active) return;
        it=markdownDocuments.emplace(v.markdownBuffer,MarkdownDocument{}).first;
    }
    auto& saved=it->second;
    if(saved.doc!=doc) saved={doc,true,false};
    // The host can reset the first fold level during opening or lexer setup.
    // Check the first header even when no text modification was reported.
    const bool firstChanged=active && saved.firstHeader>=0 &&
        send(w,sci::GetFoldLevel,saved.firstHeader)!=saved.firstLevel;
    if(!saved.dirty && saved.active==active && !firstChanged) return;
    if(!active && !saved.active) return;

    const auto count=send(w,sci::GetLineCount);
    std::vector<int> levels(static_cast<size_t>(count),0x400);
    LRESULT firstHeader=-1;
    if(active) {
        MarkdownScanner scanner;
        int depth=0;
        for(LRESULT line=0;line<count;++line) {
            const auto raw=readLine(w,line);
            std::string_view text=raw;
            if(line==0 && text.substr(0,3)=="\xEF\xBB\xBF") text.remove_prefix(3);
            const auto heading=scanner.heading(text);
            if(heading) depth=heading;
            levels[line]=0x400+(heading?heading-1:depth);
            if(heading) levels[line]|=0x2000;
        }
        for(LRESULT line=0;line<count;++line) {
            const auto current=levels[line]&0xfff;
            const auto next=line+1<count?levels[line+1]&0xfff:current;
            if(next<=current) levels[line]&=~0x2000;
            levels[line]|=next<<16;
            if(firstHeader<0 && (levels[line]&0x2000)) firstHeader=line;
        }
    }
    bool changed=!active;
    for(LRESULT line=0;!changed && line<count;++line)
        changed=send(w,sci::GetFoldLevel,line)!=levels[line];
    if(changed) {
        // Fold levels belong to the document; collapsed state belongs to each view.
        struct Pane {HWND window;LRESULT top,offset;std::vector<LRESULT> collapsed;};
        std::vector<Pane> panes;
        for(const auto& view:views) if(view.window && send(view.window,sci::GetDocPointer)==doc) {
            const auto first=send(view.window,sci::GetFirstVisibleLine);
            const auto top=send(view.window,sci::DocLineFromVisible,first);
            Pane pane{view.window,top,first-send(view.window,2220,top),{}};
            if(active) for(LRESULT line=0;line<count;++line)
                if((send(w,sci::GetFoldLevel,line)&0x2000) && !send(view.window,sci::GetFoldExpanded,line))
                    pane.collapsed.push_back(line);
            panes.push_back(std::move(pane));
        }
        applyingCss=true; // Suppress recursive updates during fold-level changes.
        struct Reset {~Reset(){applyingCss=false;}} reset;
        for(const auto& pane:panes) send(pane.window,2662,1);
        for(LRESULT line=0;line<count;++line)
            if(send(w,sci::GetFoldLevel,line)!=levels[line]) send(w,2222,line,levels[line]);
        if(!active) send(w,4003,0,-1); // Let the original lexer restore its folds.
        for(const auto& pane:panes) {
            for(auto line=pane.collapsed.rbegin();line!=pane.collapsed.rend();++line)
                if(levels[*line]&0x2000) send(pane.window,2237,*line,0);
            const auto top=send(pane.window,2220,pane.top);
            if(top>=0) send(pane.window,2613,top+pane.offset);
        }
    }
    saved.active=active;saved.dirty=false;
    saved.firstHeader=firstHeader;
    saved.firstLevel=firstHeader>=0?levels[firstHeader]:0;
}
