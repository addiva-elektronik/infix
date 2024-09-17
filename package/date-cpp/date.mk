DATE_CPP_VERSION = 3.0.1
DATE_CPP_SITE = $(call github,HowardHinnant,date,v$(DATE_CPP_VERSION))
DATE_CPP_INSTALL_STAGING = YES
DATE_CPP_LICENSE = MIT
DATE_CPP_LICENSE_FILES = LICENSE.txt
DATE_CPP_CONF_OPTS = \
	 -DBUILD_TZ_LIB=ON \
	 -DUSE_SYSTEM_TZ_DB:BOOL=ON \
	 -DENABLE_DATE_TESTING=OFF
$(eval $(cmake-package))