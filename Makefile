all: hidraw uhid

uhid: -luuid

clean:
	rm -f hidraw uhid

.PHONY: all clean
