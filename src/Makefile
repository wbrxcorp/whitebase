rwildcard=$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))
OBJS=$(subst .cpp,.o,$(call rwildcard,,*.cpp)) $(subst .c,.o,$(call rwildcard,,*.c))
BINS=$(subst .cpp,.bin,$(call rwildcard,,*.cpp))
HEADERS=$(call rwildcard,,*.h)

CXXFLAGS ?= -std=c++2a $(shell pkg-config gtkmm-4.0 --cflags)
LIBS ?= -lpam -lpam_misc -liniparser4 -lmount -lblkid -lsystemd -lutil -lssl -lcrypto -licuuc -lyajl -lvterm -lwayland-client -lsmartcols -luuid -lcurl -lqrencode $(shell pkg-config gtkmm-4.0 --libs)

all: wb wg-walbrix messages.mo vm

libproj.a: $(OBJS)
	ar r $@ $(OBJS)

%.bin: %.cpp libproj.a
	g++ $(CXXFLAGS) -g -o $@ -D__MAIN_MODULE__ $< -L . -lproj $(LIBS)

%.o: %.cpp $(HEADERS)
	g++ $(CXXFLAGS) $(DEBUGFLAGS) -c $< -o $@

%.o: %.c $(HEADERS)
	gcc $(CFLAGS) -c $< -o $@

wb: wb.cpp libproj.a
	g++ $(CXXFLAGS) -o $@ -D__MAIN_MODULE__ wb.cpp -L . -lutil -licuuc -lvterm -lpam -lmount -lblkid -lbtrfsutil -lsmartcols -lyajl -lproj -lwayland-client -luuid -lssl -lcrypto -lcurl -lqrencode -liniparser4 $(shell pkg-config gtkmm-4.0 --libs)

wg-walbrix: wg-walbrix.cpp crypt.o common.o
	g++ $(CXXFLAGS) -o $@ -D__MAIN_MODULE__ $^ -lyajl -lcurl -lssl -lcrypto -liniparser4 -lutil

libnss_wg_walbrix.so: wg-walbrix.cpp crypt.cpp
	g++ $(CXXFLAGS) -o $@ -D NSS_MODULE $^ -fPIC -shared -Wl,-soname,libnss_wg_walbrix.so.2 -lyajl -lssl -lcrypto -liniparser4

vm: vm.cpp common.o
	g++ $(CXXFLAGS) -o $@ -D__MAIN_MODULE__ $^ -lmount -lsystemd -liniparser4 -luuid -lutil -lyajl -lcurl -lblkid

messages.mo: messages.po
	msgfmt -o $@ $<

install: wb wg-walbrix messages.mo
	cp -a wb vm /usr/sbin/
	mkdir -p /usr/share/wb/themes/default
	cp -a default_theme/. /usr/share/wb/themes/default/
	cp -a messages.mo /usr/share/locale/ja/LC_MESSAGES/

clean:
	rm -f $(BINS) $(OBJS) libproj.a wb wg-walbrix vm
