include $(TOPDIR)/rules.mk

PKG_NAME:=iface-conflict
PKG_RELEASE:=1
PKG_VERSION:=1.0.0

include $(INCLUDE_DIR)/package.mk

define Package/iface-conflict
	CATEGORY:=Base system
	TITLE:=interface conflict detector
	DEPENDS:=+libubus +libubox +libblobmsg-json
endef

define Package/iface-conflict/description
	This is a monitoring daemon that emits ubus events when interface conflicts are detected.
endef

define Package/iface-conflict/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/build/iface-conflict $(1)/usr/bin

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/iface-conflict $(1)/etc/init.d/iface-conflict

endef

$(eval $(call BuildPackage,iface-conflict))
