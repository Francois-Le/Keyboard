#pragma once

struct Forced
{
  Forced(Key k) : m_key(k) {}
  Key m_key;
};

struct K
{
  K() : m_key0(Key::NONE), m_key1(Key::NONE), m_mediaKey(MediaKey::NONE), m_forcedKey(Key::NONE) {}
  K(Key key0) : m_key0(key0), m_key1(Key::NONE), m_mediaKey(MediaKey::NONE), m_forcedKey(Key::NONE) {}
  K(Key key0, Key key1) : m_key0(key0), m_key1(key1), m_mediaKey(MediaKey::NONE), m_forcedKey(Key::NONE) {}
  K(Key key0, Forced key1) : m_key0(key0), m_key1(Key::NONE), m_mediaKey(MediaKey::NONE), m_forcedKey(key1.m_key) {}
  K(MediaKey key) : m_key0(Key::NONE), m_key1(Key::NONE), m_mediaKey(key), m_forcedKey(Key::NONE) {}

  Key m_key0;
  Key m_key1;
  MediaKey m_mediaKey;
  Key m_forcedKey;
};

K baseLayer[5][12] =
{
  {K(), K(),       K(),         K(),          K(),           K(),           /**/ K(),               K(),         K(),                     K(),          K(),        K()},
  {K(), K(Key::A), K(Key::Z),   K(Key::E),    K(Key::R),     K(Key::T),     /**/ K(Key::Y),         K(Key::U),   K(Key::I),               K(Key::O),    K(Key::P),  K()},
  {K(), K(Key::Q), K(Key::S),   K(Key::D),    K(Key::F),     K(Key::G),     /**/ K(Key::H),         K(Key::J),   K(Key::K),               K(Key::L),    K(Key::M),  K()},
  {K(), K(Key::W), K(Key::X),   K(Key::C),    K(Key::V),     K(Key::B),     /**/ K(Key::BACKSPACE), K(Key::N),   K(Key::SHIFT, Key::N2 ), K(Key::N1),   K(Key::N2), K()},
  {K(), K(),       K(),         K(Key::CTRL), K(Key::SHIFT), K(Key::SPACE), /**/ K(Key::ENTER),     K(Key::ALT), K(Key::WIN),             K(),          K(),        K()},
};


K shiftLayer[5][12] =
{
  {K(), K(),                     K(),                     K(),                      K(),                    K(),                       /**/ K(),                           K(),                     K(),                     K(),                     K(),                    K()},
  {K(), K(Key::SHIFT, Key::A),   K(Key::SHIFT, Key::Z),   K(Key::SHIFT, Key::E),    K(Key::SHIFT, Key::R),  K(Key::SHIFT, Key::T),     /**/ K(Key::SHIFT, Key::Y),         K(Key::SHIFT, Key::U),   K(Key::SHIFT, Key::I),   K(Key::SHIFT, Key::O),   K(Key::SHIFT, Key::P),  K()},
  {K(), K(Key::SHIFT, Key::Q),   K(Key::SHIFT, Key::S),   K(Key::SHIFT, Key::D),    K(Key::SHIFT, Key::F),  K(Key::SHIFT, Key::G),     /**/ K(Key::SHIFT, Key::H),         K(Key::SHIFT, Key::J),   K(Key::SHIFT, Key::K),   K(Key::SHIFT, Key::L),   K(Key::SHIFT, Key::M),  K()},
  {K(), K(Key::SHIFT, Key::W),   K(Key::SHIFT, Key::X),   K(Key::SHIFT, Key::C),    K(Key::SHIFT, Key::V),  K(Key::SHIFT, Key::B),     /**/ K(Key::SHIFT, Key::BACKSPACE), K(Key::SHIFT, Key::N),   K(Key::N4 ),             K(Key::SHIFT, Key::N1),  K(Key::N3),             K()},
  {K(), K(),                     K(),                     K(Key::SHIFT, Key::CTRL), K(),                    K(Key::SHIFT, Key::SPACE), /**/ K(Key::SHIFT, Key::ENTER),     K(Key::SHIFT, Key::ALT), K(Key::SHIFT, Key::WIN), K(),                     K(),                    K()},
};

K functionLayer[5][12] =
{
  {K(), K(),        K(),         K(),          K(),           K(),           /**/ K(),                      K(),                           K(),          K(),            K(),           K()},
  {K(), K(Key::F1), K(Key::F2),  K(Key::F3),   K(Key::F4),    K(Key::ESC),   /**/ K(MediaKey::VOLUME_UP),   K(Key::TAB, Forced(Key::ALT)), K(Key::UP),   K(Key::MENU),   K(Key::HOME),  K()},
  {K(), K(Key::F5), K(Key::F6),  K(Key::F7),   K(Key::F8),    K(Key::TAB),   /**/ K(MediaKey::VOLUME_DOWN), K(Key::LEFT),                  K(Key::DOWN), K(Key::RIGHT),  K(Key::END),   K()},
  {K(), K(Key::F9), K(Key::F10), K(Key::F11),  K(Key::F12),   K(),           /**/ K(Key::DEL),              K(Key::ENTER),                 K(Key::P_UP), K(Key::P_DOWN), K(Key::PAUSE), K()},
  {K(), K(),        K(),         K(Key::CTRL), K(Key::SHIFT), K(Key::SPACE), /**/ K(Key::ENTER),            K(Key::ALT),                   K(Key::WIN),  K(),            K(),           K()},
}; 

K accentLayer[5][12] =
{
  {K(), K(),                    K(),        K(),                    K(),                    K(),                   /**/ K(),                    K(),                    K(),                    K(),                    K(),                   K(),},
  {K(), K(Key::D0),             K(Key::D2), K(Key::D7),             K(Key::RALT, Key::D6),  K(Key::D4),            /**/ K(Key::SHIFT, Key::N3), K(Key::NUM_7),          K(Key::NUM_8),          K(Key::NUM_9),          K(Key::NUM_MINUS),     K(),},
  {K(), K(Key::RALT, Key::D0),  K(Key::P2), K(Key::RALT, Key::E),   K(Key::RALT, Key::D3),  K(Key::D3),            /**/ K(Key::M2),             K(Key::NUM_4),          K(Key::NUM_5),          K(Key::NUM_6),          K(Key::NUM_PLUS),      K(),},
  {K(), K(),                    K(),        K(Key::D9),             K(Key::D8),             K(Key::D1),            /**/ K(Key::BACKSPACE),      K(Key::NUM_1),          K(Key::NUM_2),          K(Key::NUM_3),          K(Key::D02),           K(),},
  {K(), K(Key::WIN),            K(),        K(Key::CTRL),           K(Key::SHIFT),          K(),                   /**/ K(Key::ENTER),          K(Key::NUM_0),          K(Key::SHIFT, Key::N2), K(),                    K(),                   K(),},
};  

 K accentLayer2[5][12] =
{
  {K(), K(), K(), K(), K(), K(),                    /**/ K(),                   K(),                    K(),         K(),                    K(),                    K(),},
  {K(), K(), K(), K(), K(), K(Key::RALT, Key::D7),  /**/ K(Key::RALT, Key::D8), K(Key::RALT, Key::D02), K(Key::D01), K(Key::RALT, Key::D01), K(Key::SHIFT, Key::W1), K(),},
  {K(), K(), K(), K(), K(), K(Key::P1),             /**/ K(Key::RALT, Key::D2), K(Key::RALT, Key::D4),  K(Key::D5),  K(Key::RALT, Key::D5),  K(Key::W1),             K(),},
  {K(), K(), K(), K(), K(), K(Key::SHIFT, Key::M1), /**/ K(),                   K(),                    K(),         K(),                    K(),                    K(),},
  {K(), K(), K(), K(), K(), K(),                    /**/ K(),                   K(),                    K(),         K(),                    K(),                    K(),},
};

bool immuneToReset[5][12] =
{
  {false, false, false, false, false, false, /**/ false, false, false, false, false, false},
  {false, false, false, false, false, false, /**/ false, false, false, false, false, false},
  {false, false, false, false, false, false, /**/ false, false, false, false, false, false},
  {false, false, false, false, false, false, /**/ false, false, false, false, false, false},
  {false, false, false, true,  true,  false, /**/ false, false, true,  false, false, false},
};

bool onRelease[5][12] =
{
  {false, false, false, false, false, false, /**/ false, false, false, false, false, false},
  {false, false, false, false, false, false, /**/ false, false, false, false, false, false},
  {false, false, false, false, false, false, /**/ false, false, false, false, false, false},
  {false, false, false, false, false, false, /**/ false, false, false, false, false, false},
  {false, false, false, false, false, true,  /**/ true,  false, false, false, false, false},
};

enum LayerBit : uint8_t
{
  LAYER_NONE = 0,
  LAYER_SHIFT = 1,
  LAYER_FUNCTION = 2,
  LAYER_ACCENT = 3,
};

LayerBit s_layerKeys[5][12] =
{
  {LAYER_NONE,  LAYER_NONE, LAYER_NONE, LAYER_NONE,     LAYER_NONE,   LAYER_NONE,   /**/ LAYER_NONE,     LAYER_NONE, LAYER_NONE, LAYER_NONE, LAYER_NONE, LAYER_NONE},
  {LAYER_NONE,  LAYER_NONE, LAYER_NONE, LAYER_NONE,     LAYER_NONE,   LAYER_NONE,   /**/ LAYER_NONE,     LAYER_NONE, LAYER_NONE, LAYER_NONE, LAYER_NONE, LAYER_NONE},
  {LAYER_NONE,  LAYER_NONE, LAYER_NONE, LAYER_NONE,     LAYER_NONE,   LAYER_NONE,   /**/ LAYER_NONE,     LAYER_NONE, LAYER_NONE, LAYER_NONE, LAYER_NONE, LAYER_NONE},
  {LAYER_NONE,  LAYER_NONE, LAYER_NONE, LAYER_NONE,     LAYER_NONE,   LAYER_NONE,   /**/ LAYER_NONE,     LAYER_NONE, LAYER_NONE, LAYER_NONE, LAYER_NONE, LAYER_NONE},
  {LAYER_NONE,  LAYER_NONE, LAYER_NONE, LAYER_NONE,     LAYER_SHIFT,  LAYER_ACCENT, /**/ LAYER_FUNCTION, LAYER_NONE, LAYER_NONE, LAYER_NONE, LAYER_NONE, LAYER_NONE},
};

K s_keyMaps [8][5][12] =
{
  baseLayer,
  shiftLayer,
  functionLayer,
  functionLayer,
  accentLayer,
  accentLayer,
  accentLayer2,
  accentLayer2
};
