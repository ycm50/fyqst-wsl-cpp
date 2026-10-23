# fyweb - C++ 版绯雨骑士团 Web 层 + 辅助服务 + Node 原生插件
CXX      ?= g++
# 必须开 TCP_NODELAY：否则 keep-alive 下响应会撞上 TCP delayed-ACK，
# 实测每请求多 40ms，吞吐从 19000 req/s 掉到 1100 req/s
CPPFLAGS ?= -I/usr/include/mariadb -DCPPHTTPLIB_TCP_NODELAY=1
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wno-unused-parameter -pthread
LDFLAGS  ?= -pthread
LIBS     ?= -lmariadb

NODE_INC ?= /opt/node6/include/node

all: fyweb fyversion fylog native/fynative.node

# ---------------------------------------------------------------- C++ 服务
fyweb: src/common.o src/main.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

fyversion: src/fyversion.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

fylog: src/fylog.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.cpp src/common.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

src/fyversion.o: src/fyversion.cpp src/json.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

src/fylog.o: src/fylog.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

# ---------------------------------------------------------------- Node 6 原生插件
# 直接用 node 6 头文件编译，不需要 node-gyp / python2
native/fynative.node: native/fynative.cpp
	$(CXX) -shared -fPIC -O2 -std=c++11 -I$(NODE_INC) -o $@ $<

clean:
	rm -f src/*.o fyweb fyversion fylog native/fynative.node

install: all
	install -m 755 fyweb fyversion fylog /opt/fyweb/

.PHONY: all clean install
