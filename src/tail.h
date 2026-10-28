#pragma once
#include <string>
#include <cctype>
#include <vector>

namespace foldtail {
// Internal contexts for JavaScript styled by the HTML lexer, not lexer IDs.
inline constexpr int htmlJavaScript=-1,htmlAspJavaScript=-2;
inline constexpr int htmlCss=-3;
// Lexilla lexer IDs and comment style IDs (SciLexer.h).
inline bool supported(int lexer) {
    return lexer == htmlCss || lexer == htmlJavaScript || lexer == htmlAspJavaScript ||
        lexer == 2 || lexer == 3 || lexer == 35 || lexer == 38 || lexer == 6 || lexer == 4 ||
        lexer == 5 || lexer == 48 || lexer == 69 || lexer == 86 || lexer == 88 || lexer == 111 || lexer == 120;
}
inline bool comment(int lexer, int style) {
    if (lexer == htmlCss) return style == 9;
    if (lexer == htmlJavaScript) return style >= 42 && style <= 44;
    if (lexer == htmlAspJavaScript) return style >= 57 && style <= 59;
    if (lexer == 2) return style == 1 || style == 12;
    if (lexer == 48) return style == 1; // YAML # comments, excluding scalar text.
    if (lexer == 4 || lexer == 5) return style == 9;
    if (lexer == 3 || lexer == 35)
        return style == 1 || style == 2 || style == 3 || style == 15 || style == 17 || style == 18;
    if (lexer == 38) return style == 9;
    if (lexer == 6) return style == 2 || style == 3;
    if (lexer == 120) return style == 6 || style == 7;
    if (lexer == 69) return style == 124 || style == 125;
    if (lexer == 86) return style == 1;
    if (lexer == 88) return style == 1 || style == 13 || style == 16;
    if (lexer == 111) return style >= 1 && style <= 4;
    return false;
}
inline int operatorStyle(int lexer) {
    if(lexer==htmlCss) return 5;
    if(lexer==htmlJavaScript) return 50; // SCE_HJ_SYMBOLS
    if(lexer==htmlAspJavaScript) return 65; // SCE_HJA_SYMBOLS
    return lexer==69?127:lexer==111?16:lexer==88?6:(lexer==120 || lexer==86)?8:lexer==38?5:10;
}
inline int singleLineCommentStyle(int lexer) {
    if(lexer==htmlCss) return 9; // Use HTML's comment colour for embedded CSS comments.
    if(lexer==htmlJavaScript) return 43;
    if(lexer==htmlAspJavaScript) return 58;
    if(lexer==3 || lexer==35 || lexer==6 || lexer==111) return 2;
    if(lexer==69) return 125;
    if(lexer==4 || lexer==5 || lexer==38) return 9;
    if(lexer==120) return 6;
    return 1; // Python, YAML, PowerShell and R.
}
inline bool blockComment(int lexer,int style) {
    return (lexer==htmlCss && style==9) ||
        (lexer==htmlJavaScript && (style==42 || style==44)) ||
        (lexer==htmlAspJavaScript && (style==57 || style==59)) ||
        ((lexer==3 || lexer==35) && (style==1 || style==3 || style==17 || style==18)) ||
        (lexer==38 && style==9) || (lexer==69 && style==124) ||
        (lexer==111 && (style==1 || style==3));
}
struct HtmlStart { size_t end=std::string::npos; std::string name; };
inline bool htmlNameChar(unsigned char c) {return std::isalnum(c) || c=='-' || c==':' || c=='_';}
inline std::string lowerTag(std::string text) {
    for(auto& c:text) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
template<class StyleAt> HtmlStart htmlStart(const std::string& text,StyleAt styleAt,bool xml=false) {
    const auto start=text.find_first_not_of(" \t");
    if(start==std::string::npos || start+1>=text.size() || text[start]!='<' ||
        !std::isalpha(static_cast<unsigned char>(text[start+1])) ||
        (styleAt(start)!=1 && styleAt(start)!=2)) return {};
    auto nameEnd=start+1;
    while(nameEnd<text.size() && htmlNameChar(static_cast<unsigned char>(text[nameEnd]))) ++nameEnd;
    char quote=0;
    for(auto i=nameEnd;i<text.size();++i) {
        const auto c=text[i];
        if(quote) {if(c==quote) quote=0;continue;}
        if(c=='\'' || c=='"') {quote=c;continue;}
        if(c=='>') {
            auto previous=i;
            while(previous>nameEnd && std::isspace(static_cast<unsigned char>(text[previous-1]))) --previous;
            if(previous>nameEnd && text[previous-1]=='/') return {};
            auto name=text.substr(start+1,nameEnd-start-1);
            return {i+1,xml?name:lowerTag(name)};
        }
    }
    return {};
}
template<class StyleAt> std::string htmlTail(const std::string& text,const std::string& name,StyleAt styleAt,bool xml=false) {
    for(size_t start=0;(start=text.find("</",start))!=std::string::npos;++start) {
        if(styleAt(start)!=1 && styleAt(start)!=2) continue;
        auto end=start+2;
        while(end<text.size() && htmlNameChar(static_cast<unsigned char>(text[end]))) ++end;
        auto closingName=text.substr(start+2,end-start-2);
        if((xml?closingName:lowerTag(closingName))!=name) continue;
        auto close=end;
        while(close<text.size() && std::isspace(static_cast<unsigned char>(text[close]))) ++close;
        if(close>=text.size() || text[close]!='>') continue;
        const auto last=text.find_last_not_of(" \t\r\n");
        return text.substr(start,last-start+1);
    }
    return {};
}
template<class StyleAt>
std::string tail(const std::string& text, int lexer, StyleAt styleAt) {
    if (!supported(lexer)) return "";
    std::string out;
    size_t brace=std::string::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        const auto style=styleAt(i);
        if (comment(lexer, style)) continue;
        // Only syntax operators count: braces inside strings are not block endings.
        const bool op=style==operatorStyle(lexer);
        if (brace==std::string::npos && (text[i]=='}' || (lexer==120 && text[i]==']')) && op) brace=out.size();
        out += text[i];
    }
    const auto start = brace!=std::string::npos ? brace : out.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const auto end = out.find_last_not_of(" \t\r\n");
    out = out.substr(start, end - start + 1);
    // A closing brace keeps its entire non-comment suffix, including arguments.
    if (out.size() > 256) return "";
    if (brace==std::string::npos && out.find_first_not_of("] );,\t")!=std::string::npos) return "";
    if (out.find_first_of("}])") == std::string::npos) return "";
    return out;
}
inline std::string display(const std::string& suffix) {
    return suffix.empty() ? "" : "\xE2\x80\xA6 " + suffix;
}
inline std::string withoutEol(std::string text) {
    while(!text.empty() && (text.back()=='\r' || text.back()=='\n')) text.pop_back();
    return text;
}
inline std::wstring preview(const std::vector<std::wstring>& lines,bool more=false) {
    std::wstring result;
    const auto count=lines.size()>10?10:lines.size();
    for(size_t i=0;i<count;++i) {
        auto line=lines[i];
        while(!line.empty() && (line.back()==L'\r' || line.back()==L'\n')) line.pop_back();
        if(i) result+=L"\r\n";
        result+=line;
    }
    if(more || lines.size()>10) {
        size_t indent=0;
        // Centre approximately below the final visible nonblank line.
        for(size_t i=count;i>0;--i) {
            const auto& line=lines[i-1];
            const auto first=line.find_first_not_of(L" \t\r\n");
            const auto last=line.find_last_not_of(L" \t\r\n");
            if(first==std::wstring::npos) continue;
            size_t column=0,leading=0;
            for(size_t j=0;j<=last;++j) {
                if(j==first) leading=column;
                const auto c=line[j];
                if(c==L'\t') column+=4-column%4;
                else column+=((c>=0x2E80 && c<=0xA4CF) ||
                    (c>=0xAC00 && c<=0xD7A3) || (c>=0xF900 && c<=0xFAFF) ||
                    (c>=0xFF01 && c<=0xFF60))?2:1;
            }
            indent=leading+(column-leading)/2;
            break;
        }
        result+=L"\r\n"+std::wstring(indent,L' ')+L"\u22EE";
    }
    return result;
}
}
