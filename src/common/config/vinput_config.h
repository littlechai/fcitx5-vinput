#pragma once

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>

#include <memory>

inline constexpr const char *kVinputConfigPath = "conf/vinput.conf";

FCITX_CONFIG_ENUM(TriggerMode, Tap, Hold, Both)
FCITX_CONFIG_ENUM_I18N_ANNOTATION(TriggerMode, N_("Tap"), N_("Hold"), N_("Both"))

struct VinputSettings {
  fcitx::KeyList triggerKeys{fcitx::Key(FcitxKey_Alt_R)};
  fcitx::KeyList commandKeys{fcitx::Key(FcitxKey_Control_R)};
  fcitx::KeyList sceneMenuKeys{
      fcitx::Key(FcitxKey_Shift_R)};
  fcitx::KeyList asrMenuKeys{
      fcitx::Key(FcitxKey_F8)};
  fcitx::KeyList pagePrevKeys{
      fcitx::Key(FcitxKey_Page_Up),
      fcitx::Key(FcitxKey_KP_Page_Up),
  };
  fcitx::KeyList pageNextKeys{
      fcitx::Key(FcitxKey_Page_Down),
      fcitx::Key(FcitxKey_KP_Page_Down),
  };
  TriggerMode triggerMode{TriggerMode::Both};
};

class VinputConfig : public fcitx::Configuration {
public:
  VinputConfig(const VinputSettings &settings);
  VinputConfig(const VinputConfig &) = delete;
  VinputConfig &operator=(const VinputConfig &) = delete;

  const char *typeName() const override { return "VinputConfig"; }
  VinputSettings settings() const;

  fcitx::Option<fcitx::KeyList, fcitx::ListConstrain<fcitx::KeyConstrain>,
                fcitx::DefaultMarshaller<fcitx::KeyList>,
                fcitx::ToolTipAnnotation>
      triggerKeys;

  fcitx::Option<fcitx::KeyList, fcitx::ListConstrain<fcitx::KeyConstrain>,
                fcitx::DefaultMarshaller<fcitx::KeyList>,
                fcitx::ToolTipAnnotation>
      commandKeys;

  fcitx::Option<fcitx::KeyList, fcitx::ListConstrain<fcitx::KeyConstrain>,
                fcitx::DefaultMarshaller<fcitx::KeyList>,
                fcitx::ToolTipAnnotation>
      sceneMenuKeys;

  fcitx::Option<fcitx::KeyList, fcitx::ListConstrain<fcitx::KeyConstrain>,
                fcitx::DefaultMarshaller<fcitx::KeyList>,
                fcitx::ToolTipAnnotation>
      asrMenuKeys;

  fcitx::Option<fcitx::KeyList, fcitx::ListConstrain<fcitx::KeyConstrain>,
                fcitx::DefaultMarshaller<fcitx::KeyList>,
                fcitx::ToolTipAnnotation>
      pagePrevKeys;

  fcitx::Option<fcitx::KeyList, fcitx::ListConstrain<fcitx::KeyConstrain>,
                fcitx::DefaultMarshaller<fcitx::KeyList>,
                fcitx::ToolTipAnnotation>
      pageNextKeys;

  fcitx::Option<TriggerMode, fcitx::NoConstrain<TriggerMode>,
                fcitx::DefaultMarshaller<TriggerMode>,
                TriggerModeI18NAnnotation>
      triggerMode;

  fcitx::ExternalOption modelManager;
};

VinputSettings LoadVinputSettings();
bool SaveVinputSettings(const VinputSettings &settings);
std::unique_ptr<VinputConfig> BuildVinputConfig(const VinputSettings &settings);
