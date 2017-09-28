
#include <iostream>
#include <cstring>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>

// Глобальные переменные
static std::string gArgFormat ="Wrong argumets, correct format: '[-n NNN] infilename outfilename'";

class Buffer {
public:
    Buffer(size_t aMaxSize) : m_maxSize(aMaxSize), m_size(0) {
        if (!aMaxSize) {
            throw (std::runtime_error("Can't alloc buffer"));
        }
        m_data = new char[aMaxSize];
    }
   ~Buffer() {
       if (m_data) {
           delete [] m_data;
       }
   }

    void add(const char * aData, size_t aSize) {
        if (!aData || !aSize) {
            throw (std::runtime_error(std::string("Wrong param on adding to buffer")));
        }
        if (aSize + m_size > m_maxSize) {
            throw (std::runtime_error(std::string("Too much data to add to buffer")));
        }
        memcpy(m_data + m_size, aData, aSize);
        m_size += aSize;
    }
    void shift(size_t aShift) {
        if (aShift >= m_size) {
            m_size = 0;
            return;
        }
        memmove(m_data, m_data + aShift, m_size - aShift);
        m_size -= aShift;
    }
    char * getData() const { return m_data; }
    size_t getSize() const { return m_size; }

private:
    char * m_data;
    size_t m_size;
    size_t m_maxSize;
};

class Parser {
public:
    Parser(FILE * aInFile, FILE * aOutFile, size_t aMentions) :
            m_inFile(aInFile),
            m_outFile(aOutFile),
            m_mentions(aMentions),
            m_buffer(8192)
    {
    }

    void getDomains()
    {
        std::string domain;
        std::string path;
        resetPtrs();
        char readBuffer[1024];
        while (!feof(m_inFile) && !ferror(m_inFile)) {
            size_t bytesRead = fread(readBuffer, 1, sizeof(readBuffer), m_inFile);
            if (!bytesRead) {
                continue;
            }
            m_buffer.add(readBuffer, bytesRead);
            m_end = m_buffer.getData() + m_buffer.getSize();
            while (m_currPtr < m_end) {
                switch(m_parseState) {
                    case psH:
                        if (*m_currPtr == 'h') {
                            ++m_currPtr;
                            m_parseState = psT1;
                        }
                        else {
                            m_buffer.shift(1);
                            resetPtrs();
                            m_parseState = psH;
                        }
                        break;
                    case psT1:
                        if (*m_currPtr == 't') {
                            ++m_currPtr;
                            m_parseState = psT2;
                        }
                        else {
                            m_buffer.shift(2);
                            resetPtrs();
                            m_parseState = psH;
                        }
                        break;
                    case psT2:
                        if (*m_currPtr == 't') {
                            ++m_currPtr;
                            m_parseState = psP;
                        }
                        else {
                            m_buffer.shift(3);
                            resetPtrs();
                            m_parseState = psH;
                        }
                        break;
                    case psP:
                        if (*m_currPtr == 'p') {
                            ++m_currPtr;
                            m_parseState = psS;
                        }
                        else {
                            m_buffer.shift(4);
                            resetPtrs();
                            m_parseState = psH;
                        }
                        break;
                    case psS:
                        if (*m_currPtr == 's') {
                            ++m_currPtr;
                            m_parseState = psDots;
                            m_https = true;
                        }
                        else {
                            if (*m_currPtr == ':') {
                                m_parseState = psDots;;
                            }
                            else {
                                m_buffer.shift(5);
                                resetPtrs();
                                m_parseState = psH;
                            }
                        }
                        // todo дальнейшее смещение должно быть иным
                        break;
                    case psDots:
                        if (*m_currPtr == ':') {
                            ++m_currPtr;
                            m_parseState = psSlash1;
                        }
                        else {
                            m_buffer.shift(5);
                            resetPtrs();
                            m_parseState = psH;
                        }
                        break;
                    case psSlash1:
                        if (*m_currPtr == '/') {
                            ++m_currPtr;
                            m_parseState = psSlash2;
                        }
                        else {
                            m_buffer.shift(6);
                            resetPtrs();
                            m_parseState = psH;
                        }
                        break;
                    case psSlash2:
                        if (*m_currPtr == '/') {
                            ++m_currPtr;
                            m_parseState = psDomain;
                        }
                        else {
                            m_buffer.shift(6);
                            resetPtrs();
                            m_parseState = psH;
                        }
                        break;
                    case psDomain:
                        if (std::isalpha(*m_currPtr) || std::isalnum(*m_currPtr) || *m_currPtr == '.' || *m_currPtr == '-') {
                            domain.push_back(*m_currPtr);
                            ++m_currPtr;
                        }
                        else {
                            if (m_domains.find(domain) == m_domains.end()) {
                                m_domains[domain] = 1;
                            }
                            else {
                                ++m_domains[domain];
                            }
                            ++m_count;
                            domain.clear();
                            m_buffer.shift(m_currPtr - m_begin);
                            resetPtrs();
                            m_parseState = psPath;
                        }
                        break;
                    case psPath:
                        if (std::isalpha(*m_currPtr) || std::isalnum(*m_currPtr) || *m_currPtr == '.' || *m_currPtr == ',' || *m_currPtr == '/' || *m_currPtr == '+' || *m_currPtr == '_') {
                            path.push_back(*m_currPtr);
                            ++m_currPtr;
                        }
                        else {
                            if (path.empty()) {
                                path = "/";
                            }
                            if (m_paths.find(path) == m_paths.end()) {
                                m_paths[path] = 1;
                            }
                            else {
                                ++m_paths[path];
                            }
                            path.clear();
                            m_buffer.shift(m_currPtr - m_begin);
                            resetPtrs();
                            m_parseState = psH;
                        }
                        break;
                    default:
                        throw(std::runtime_error(std::string("Wrong state machine state")));
                }
            }
        }
        if (ferror(m_inFile)) {
            throw std::runtime_error("Error in input file");
        }
        if (feof(m_inFile)) {
            // Обработка события, если путь или домен в конце файла
            if (!domain.empty()) {
                if (m_domains.find(domain) == m_domains.end()) {
                    m_domains[domain] = 1;
                }
                else {
                    ++m_domains[domain];
                }
                m_count++;
            }
            if (path.empty() && (m_parseState == psPath || m_parseState == psDomain)) {
                if (m_paths.find("/") == m_paths.end()) {
                    m_paths["/"] = 1;
                }
                else {
                    ++m_paths["/"];
                }
            }
        }
    }
    void print() {
        std::vector<std::pair<size_t, std::string>> domains;
        std::vector<std::pair<size_t, std::string>> paths;

        auto lambda = [](const std::pair<size_t, std::string> &a, const std::pair<size_t, std::string> &b) -> bool {
            if (a.first < b.first) {
                return false;
            }
            else if (a.first == b.first) {
                return memcmp(a.second.c_str(), b.second.c_str(), std::min(a.second.length(), b.second.length())) < 0;
            }
            else {
                return true;
            }
        };
        for (auto &domain : m_domains) {
            domains.emplace_back(domain.second, domain.first);
        }
        for (auto &path : m_paths) {
            paths.emplace_back(path.second, path.first);
        }
        std::sort(domains.begin(), domains.end(), lambda);
        std::sort(paths.begin(), paths.end(), lambda);

        std::stringstream ss;
        ss << "total urls " << m_count << ", domains " << m_domains.size() << ", paths " << m_paths.size() << std::endl;
        fputs(ss.str().c_str(), m_outFile);

        fputs("\n", m_outFile);
        fputs("top domains\n", m_outFile);
        for (size_t i = 0; i < std::min(domains.size(), m_mentions); ++i) {
            ss.str("");
            ss << domains[i].first << " " << domains[i].second << std::endl;
            fputs(ss.str().c_str(), m_outFile);
        }
        fputs("\n", m_outFile);
        fputs("top paths\n", m_outFile);
        for (size_t i = 0; i < std::min(paths.size(), m_mentions); ++i) {
            ss.str("");
            ss << paths[i].first << " " << paths[i].second << std::endl;
            fputs(ss.str().c_str(), m_outFile);
        }
    }
private:
    FILE * m_inFile{nullptr};
    FILE * m_outFile{nullptr};
    size_t m_mentions{4};

    char *m_begin{};
    char *m_end{};
    char *m_currPtr{};
    bool m_https{false};
    size_t m_count{0};

    Buffer m_buffer;
    std::map<std::string, size_t> m_domains;
    std::map<std::string, size_t> m_paths;

    enum {
        psH,
        psT1,
        psT2,
        psP,
        psS,
        psDots,
        psSlash1,
        psSlash2,
        psDomain,
        psPath,
    } m_parseState{psH};

    void resetPtrs() {
        m_begin = m_buffer.getData();
        m_end = m_buffer.getData() + m_buffer.getSize();
        m_currPtr = m_begin;
        m_https = false;
    }
};

int main(int argc, char *argv[]) {
    //--- Проверка входных параметров
    size_t mentionsQuan;
    const char *inFileName;
    const char *outFileName;
    if (argc == 5) {
        if (strcmp(argv[1], "-n")) {
            std::cerr << gArgFormat << std::endl;
            return -1;
        }
        sscanf(argv[2], "%zu", &mentionsQuan);
        inFileName = argv[3];
        outFileName = argv[4];
    }
    else if (argc == 3) {
        inFileName = argv[1];
        outFileName = argv[2];
        mentionsQuan = 4;
    }
    else {
        std::cerr << gArgFormat << std::endl;
        return -1;
    }

    //--- Открытие файлов
    FILE * inFile;
    inFile = fopen(inFileName, "r");
    if (!inFile) {
        std::cerr << "Can't open input file: " << std::string(inFileName) << std::endl;
        return -1;
    }
    FILE * outFile;
    outFile = fopen(outFileName, "w");
    if (!outFile) {
        std::cerr << "Can't open output file: " << std::string(outFileName) << std::endl;
        return -1;
    }

    try {
        Parser parser(inFile, outFile, mentionsQuan);
        parser.getDomains();
        parser.print();
    }
    catch (std::exception &e) {
        std::cerr << "Caught exception " << e.what() << std::endl;
        return -1;
    }
    catch (...) {
        std::cerr << "Caught unknown exception " << std::endl;
        return -1;
    }

    fclose(inFile);
    fclose(outFile);
    return 0;
}