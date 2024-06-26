################################################################################
#
# mcd
#
################################################################################

MCD_VERSION = 2.3
MCD_SITE    = https://github.com/kernelkit/mcd/releases/download/v$(MCD_VERSION)
MCD_LICENSE = BSD-3-Clause
MCD_LICENSE_FILES = LICENSE
MCD_CONF_OPTS = --disable-silent-rules --with-group=wheel

define MCD_INSTALL_CONFIG
	$(INSTALL) -D -m 0644 $(@D)/mcd.conf $(TARGET_DIR)/etc/mcd.conf
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/etc/mc.d
endef
MCD_POST_INSTALL_TARGET_HOOKS += MCD_INSTALL_CONFIG

define MCD_INSTALL_FINIT_SVC
	$(INSTALL) -D -m 0644 $(MCD_PKGDIR)/mcd.svc \
		$(FINIT_D)/available/mcd.conf
	$(INSTALL) -D -m 0644 $(MCD_PKGDIR)/template.svc \
		$(FINIT_D)/available/mcd@.conf
endef

MCD_POST_INSTALL_TARGET_HOOKS += MCD_INSTALL_FINIT_SVC

$(eval $(autotools-package))
