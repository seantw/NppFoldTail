#pragma once
// CSS inside HTML has no native lexer styles or folds. Keep a separate syntax
// map and add fold levels only; never write document text or syntax colours.
struct CssDocument {
    bool dirty=true,active=false;
    int lexer=-1;
    std::vector<unsigned char> syntax; // 0 outside CSS, 1 code, 5 operator, 9 comment, 13 string.
    std::map<LRESULT,LRESULT> braces;
};
static std::map<LRESULT,CssDocument> cssDocuments;
static bool applyingCss=false;
static int syntaxStyle(HWND w,LRESULT position,int lexer) {
    if(lexer==foldtail::htmlCss) {
        const auto doc=cssDocuments.find(send(w,sci::GetDocPointer));
        if(doc!=cssDocuments.end() && position>=0 && static_cast<size_t>(position)<doc->second.syntax.size())
            return doc->second.syntax[static_cast<size_t>(position)];
        return 0;
    }
    return static_cast<int>(send(w,sci::GetStyleAt,position));
}
static void refreshCssFolds(HWND w) {
    if(applyingCss) return;
    const auto pointer=send(w,sci::GetDocPointer);
    const auto lexer=static_cast<int>(send(w,sci::GetLexer));
    const bool active=enabled && (lexer==4 || lexer==69);
    auto& saved=cssDocuments[pointer];
    if(!saved.dirty && saved.active==active && saved.lexer==lexer) return;
    if(!active && !saved.active) {
        saved.dirty=false;saved.lexer=lexer;return;
    }
    applyingCss=true;
    struct Reset {~Reset(){applyingCss=false;}} reset;
    const auto lines=send(w,sci::GetLineCount);
    const auto firstVisible=send(w,sci::GetFirstVisibleLine);
    const auto topLine=send(w,sci::DocLineFromVisible,firstVisible);
    const auto topOffset=firstVisible-send(w,2220,topLine);
    std::vector<LRESULT> collapsed;
    for(LRESULT line=0;line<lines;++line)
        if((send(w,sci::GetFoldLevel,line)&0x2000) && !send(w,sci::GetFoldExpanded,line))
            collapsed.push_back(line);
    // Rebuild from the host lexer, so previous CSS depths cannot accumulate.
    send(w,2662,1); // SCI_FOLDALL, EXPAND
    send(w,4003,0,-1); // SCI_COLOURISE: restore native HTML/JS/PHP fold levels.
    CssDocument next;next.active=active;next.lexer=lexer;
    if(active) {
        const auto length=send(w,2006); // SCI_GETLENGTH
        std::string text(static_cast<size_t>(length)+1,'\0');
        send(w,2182,text.size(),reinterpret_cast<LPARAM>(text.data())); // SCI_GETTEXT
        text.resize(static_cast<size_t>(length));
        auto lower=foldtail::lowerTag(text);
        next.syntax.resize(text.size());
        std::vector<int> opens(static_cast<size_t>(lines)),closes(static_cast<size_t>(lines));
        const auto addPair=[&](size_t start,size_t end) {
            const auto first=send(w,2166,start),last=send(w,2166,end);
            if(first<last) {++opens[static_cast<size_t>(first)];++closes[static_cast<size_t>(last)];}
        };
        for(size_t tag=0;(tag=lower.find("<style",tag))!=std::string::npos;) {
            const auto nameEnd=tag+6;
            if((nameEnd<text.size() && !std::isspace(static_cast<unsigned char>(text[nameEnd])) && text[nameEnd]!='>') ||
                (send(w,sci::GetStyleAt,tag)!=1 && send(w,sci::GetStyleAt,tag)!=2)) {tag=nameEnd;continue;}
            size_t begin=nameEnd;char quote=0;
            for(;begin<text.size();++begin) {
                const auto c=text[begin];
                if(quote) {if(c==quote) quote=0;}
                else if(c=='\'' || c=='"') quote=c;
                else if(c=='>') break;
            }
            if(begin==text.size()) break;
            ++begin;
            auto end=begin;
            while((end=lower.find("</style",end))!=std::string::npos) {
                const auto after=end+7;
                if(after==text.size() || text[after]=='>' || std::isspace(static_cast<unsigned char>(text[after]))) break;
                end=after;
            }
            if(end==std::string::npos) end=text.size();
            std::vector<size_t> stack;
            int parentheses=0;
            for(size_t i=begin;i<end;) {
                const auto c=text[i];next.syntax[i]=1;
                if(c=='/' && i+1<end && text[i+1]=='*') {
                    const auto start=i;
                    auto close=text.find("*/",i+2);
                    const bool matched=close!=std::string::npos && close+1<end;
                    const auto stop=matched?close+2:end;
                    std::fill(next.syntax.begin()+i,next.syntax.begin()+stop,9);
                    if(matched) addPair(start,close);
                    i=stop;continue;
                }
                if(c=='\'' || c=='"') {
                    quote=c;next.syntax[i++]=13;
                    while(i<end) {
                        const auto ch=text[i];next.syntax[i++]=13;
                        if(ch=='\\' && i<end) {
                            const auto escaped=text[i];next.syntax[i++]=13;
                            if(escaped=='\r' && i<end && text[i]=='\n') next.syntax[i++]=13;
                        } else if(ch==quote || ch=='\r' || ch=='\n') break;
                    }
                    continue;
                }
                if(c=='\\') {i+=std::min<size_t>(2,end-i);continue;}
                if(c=='(' && i>=begin+3 && lower.compare(i-3,3,"url")==0 &&
                    (i==begin+3 || (!std::isalnum(static_cast<unsigned char>(text[i-4])) && text[i-4]!='-' && text[i-4]!='_'))) {
                    // An unquoted URL may contain braces or /* as literal URL text.
                    next.syntax[i++]=13;char urlQuote=0;
                    while(i<end) {
                        const auto ch=text[i];next.syntax[i++]=13;
                        if(ch=='\\' && i<end) next.syntax[i++]=13;
                        else if(urlQuote) {if(ch==urlQuote) urlQuote=0;}
                        else if(ch=='\'' || ch=='"') urlQuote=ch;
                        else if(ch==')') break;
                    }
                    continue;
                }
                if(c=='(') ++parentheses;
                else if(c==')' && parentheses>0) --parentheses;
                else if(parentheses==0 && c=='{') {next.syntax[i]=5;stack.push_back(i);}
                else if(parentheses==0 && c=='}') {
                    next.syntax[i]=5;
                    if(!stack.empty()) {
                        const auto start=stack.back();stack.pop_back();
                        next.braces[static_cast<LRESULT>(start)]=static_cast<LRESULT>(i);
                        addPair(start,i);
                    }
                }
                ++i;
            }
            tag=end;
        }
        int depth=0;
        for(LRESULT line=0;line<lines;++line) {
            const auto index=static_cast<size_t>(line);
            const auto after=std::max(0,depth+opens[index]-closes[index]);
            if(depth || opens[index]) {
                const auto original=static_cast<unsigned int>(send(w,sci::GetFoldLevel,line));
                const auto use=opens[index]?std::max(0,depth-closes[index]):depth;
                const auto current=std::min<unsigned int>(0xfff,(original&0xfff)+use);
                const auto nativeNext=(original>>16)&0xfff;
                const auto following=std::min<unsigned int>(0xfff,(nativeNext?nativeNext:(original&0xfff))+after);
                auto level=(original&0xf000f000U)|current|(following<<16);
                if(opens[index]) level|=0x2000;
                send(w,2222,line,level); // SCI_SETFOLDLEVEL
            }
            depth=after;
        }
    }
    next.dirty=false;saved=std::move(next);
    // Restore children first so nested collapsed states survive their parents.
    for(auto it=collapsed.rbegin();it!=collapsed.rend();++it)
        if(send(w,sci::GetFoldLevel,*it)&0x2000) send(w,2237,*it,0); // SCI_FOLDLINE, CONTRACT
    const auto restoredTop=send(w,2220,topLine);
    if(restoredTop>=0) send(w,2613,restoredTop+topOffset); // SCI_SETFIRSTVISIBLELINE
}
