#pragma once
struct MarkdownDocument {
    LRESULT doc{};
    bool dirty=true,active=false;
    unsigned options=0;
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

static std::string_view markdownTrim(std::string_view text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==std::string_view::npos) return {};
    return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}

static bool markdownLinkDefinition(std::string_view text) {
    const auto indent=text.find_first_not_of(' ');
    if(indent>3) return false;
    text.remove_prefix(indent);
    if(text.size()<5 || text.front()!='[' || text[1]=='^') return false;
    for(size_t i=1;i<text.size();++i) {
        if(text[i]=='\\' && i+1<text.size()) {++i;continue;}
        if(text[i]=='[') return false;
        if(text[i]==']')
            return i+1<text.size() && text[i+1]==':' &&
                !markdownTrim(text.substr(1,i-1)).empty() && !markdownTrim(text.substr(i+2)).empty();
    }
    return false;
}

// Count real cell separators, ignoring escaped pipes.
static int markdownCells(std::string_view text,bool delimiter=false) {
    text=markdownTrim(text);
    if(text.empty() || text.find('|')==std::string_view::npos) return 0;
    if(text.front()=='|') text.remove_prefix(1);
    int cells=0;
    size_t start=0;
    for(size_t i=0;i<=text.size();++i) {
        if(i<text.size() && text[i]=='\\' && i+1<text.size()) {++i;continue;}
        if(i<text.size() && text[i]!='|') continue;
        if(i==text.size() && i==start) break; // Optional final pipe.
        if(delimiter) {
            auto cell=markdownTrim(text.substr(start,i-start));
            if(!cell.empty() && cell.front()==':') cell.remove_prefix(1);
            if(!cell.empty() && cell.back()==':') cell.remove_suffix(1);
            if(cell.empty() || cell.find_first_not_of('-')!=std::string_view::npos) return 0;
        }
        ++cells;start=i+1;
    }
    return cells;
}

struct MarkdownListItem {int kind{},indent{},content{};};
static bool markdownRule(std::string_view text) {
    if(text.empty() || (text.front()!='-' && text.front()!='*' && text.front()!='_')) return false;
    int marks=0;
    for(char c:text) {
        if(c==text.front()) ++marks;
        else if(c!=' ' && c!='\t') return false;
    }
    return marks>=3;
}
static MarkdownListItem markdownListItem(std::string_view text) {
    const auto indent=text.find_first_not_of(' ');
    if(indent==std::string_view::npos || indent>3) return {};
    if(markdownRule(markdownTrim(text))) return {};
    size_t end=indent;
    int kind=1;
    if(text[end]=='-' || text[end]=='+' || text[end]=='*') ++end;
    else {
        while(end<text.size() && text[end]>='0' && text[end]<='9') ++end;
        if(end==indent || end-indent>9 || end==text.size() || (text[end]!='.' && text[end]!=')')) return {};
        kind=2;++end;
    }
    if(end==text.size() || (text[end]!=' ' && text[end]!='\t')) return {};
    size_t content=end+1;
    while(content<text.size() && text[content]==' ') ++content;
    if(content-end>4) content=end+1;
    return {kind,static_cast<int>(indent),static_cast<int>(content)};
}

static void markdownLevels(const std::vector<std::string>& lines,std::vector<int>& levels) {
    MarkdownScanner scanner;
    std::vector<int> headings;
    const auto count=lines.size();
    for(size_t i=0;i<count;) {
        const std::string_view text=lines[i];
        const auto heading=scanner.heading(text);
        if(heading && markdownFolding) {
            while(!headings.empty() && headings.back()>=heading) headings.pop_back();
            levels[i++]=(0x400+static_cast<int>(headings.size()))|0x2000;
            headings.push_back(heading);
            continue;
        }
        const int level=0x400+static_cast<int>(headings.size());
        levels[i]=level;
        size_t end=i;
        if(markdownBlocks && scanner.fence) {
            // Consume the entire fence, including its closing line (or EOF).
            while(end+1<count && scanner.fence) scanner.heading(lines[++end]);
        } else if(markdownBlocks && !heading) {
            const auto indent=text.find_first_not_of(' ');
            const int cells=indent<=3?markdownCells(text):0;
            const auto item=markdownListItem(text);
            if(item.kind) {
                int content=item.content;
                for(size_t j=i+1;j<count;++j) {
                    const auto trimmed=markdownTrim(lines[j]);
                    if(trimmed.empty()) continue;
                    const auto leading=lines[j].find_first_not_of(' ');
                    if(leading>=static_cast<size_t>(content) || lines[j][leading]=='\t') {end=j;continue;}
                    const auto next=markdownListItem(lines[j]);
                    if(next.kind==item.kind && next.indent>=item.indent) {content=next.content;end=j;continue;}
                    if(next.kind || j>end+1) break;
                    MarkdownScanner boundary;
                    if(boundary.heading(lines[j]) || boundary.fence || markdownRule(trimmed) ||
                        markdownLinkDefinition(lines[j])) break;
                    const auto& previous=lines[end];
                    const bool hardBreak=previous.size()>=2 && previous.compare(previous.size()-2,2,"  ")==0;
                    if(!hardBreak && leading<=static_cast<size_t>(item.indent)) break;
                    end=j; // Indented continuation or a line after a Markdown hard break.
                }
            } else if(markdownLinkDefinition(text)) {
                while(end+1<count && markdownLinkDefinition(lines[end+1])) ++end;
            } else if(cells && i+1<count && markdownCells(lines[i+1],true)==cells) {
                end=i+1;
                while(end+1<count) {
                    const auto& row=lines[end+1];
                    MarkdownScanner boundary;
                    if(boundary.heading(row) || boundary.fence || markdownListItem(row).kind || markdownLinkDefinition(row) ||
                        row.find_first_not_of(' ')>3 || !markdownCells(row)) break;
                    ++end;
                }
            }
        }
        if(end>i) {
            levels[i]|=0x2000;
            for(size_t j=i+1;j<=end;++j) levels[j]=level+1;
        }
        i=end+1;
    }
}

static void refreshMarkdownFolds(View& v) {
    const auto w=v.window;
    const auto doc=send(w,sci::GetDocPointer);
    identifyMarkdown(v,doc);
    const auto lexer=static_cast<int>(send(w,sci::GetLexer));
    const unsigned options=(markdownFolding?1U:0U)|(markdownBlocks?2U:0U);
    const bool active=enabled && options && v.markdownFile && !foldtail::supported(lexer);
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
    if(!saved.dirty && saved.active==active && saved.options==options && !firstChanged) return;
    if(!active && !saved.active) return;

    const auto count=send(w,sci::GetLineCount);
    std::vector<int> levels(static_cast<size_t>(count),0x400);
    LRESULT firstHeader=-1;
    if(active) {
        std::vector<std::string> lines;
        lines.reserve(static_cast<size_t>(count));
        for(LRESULT line=0;line<count;++line) {
            auto raw=foldtail::withoutEol(readLine(w,line));
            if(line==0 && raw.compare(0,3,"\xEF\xBB\xBF")==0) raw.erase(0,3);
            lines.push_back(std::move(raw));
        }
        markdownLevels(lines,levels);
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
    saved.active=active;saved.dirty=false;saved.options=options;
    saved.firstHeader=firstHeader;
    saved.firstLevel=firstHeader>=0?levels[firstHeader]:0;
}
