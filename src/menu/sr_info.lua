-- Single Race mod for NFS Hot Pursuit (2010): the picker's own Lua control (WALL.BNDL control "SRInfo").
-- Port of the Remastered sr_info.lua.
-- * NavRight: the footer's right-hand prompt group. It is the game's own group (built as nav_event_map.lua builds it:
--   view profile, freedrive / feed text, back, select) with a "RACE SETTINGS" prompt on the to_main_menu input
--   (Menu/Start on the pad) put first, so it sits between DRIVER DETAILS (left group) and the game's prompts.
-- * Tracks: the location map's track count text.
-- * Summary: the info box text (helper: ResolveStringBinding("SR.Summary")).
-- * Pressing that input asks the helper to open the Race Settings menu (ResolveStringBinding("SR.OpenSettings")).

sr_info = {}

local E_RACE_SETTINGS = 1
local kRaceSettingsPrompt = '{Controls.ButtonPrompt["AUTOLOG_TO_MAIN_MENU"]} RACE SETTINGS'
local kSep = '   '   -- nav_event_map.lua's separator
-- nav_event_map.lua's table (values = indices into Autolog.Feedback.Navigation)
local kNav = { E_NAVIGATION_BLANK = 0, E_NAVIGATION_GO_BACK = 1, E_NAVIGATION_PLAY = 2, E_NAVIGATION_TELL_YOUR_FRIENDS = 3,
               E_NAVIGATION_VIEW_PROFILE = 4, E_NAVIGATION_GO_HOME = 5, E_NAVIGATION_FREEDRIVE_COP = 6,
               E_NAVIGATION_FREEDRIVE_RACER = 7, E_NAVIGATION_DRIVER_DETAILS = 8, E_NAVIGATION_SCROLL_SPEEDWALL = 9 }

local function resolveBool(b)
  local ok, v = pcall(ResolveBoolBinding, b)
  return ok and v == true
end

-- nav_event_map.lua's NavRight (the picker has no speedwall: no view-profile prompt)
local function gameNavRight(s)
  local second = ""
  local set = nav_event_map ~= nil and nav_event_map.msNavSet or nil
  if set == "locationSelect" then
    if resolveBool("") then second = s.E_NAVIGATION_FREEDRIVE_COP or ""
    else second = s.E_NAVIGATION_FREEDRIVE_RACER or "" end
  elseif set == "eventSelect" then
    if ResolveWideStringBinding ~= nil and not resolveBool("Autolog.Adapter.CurrentItem.IsLocked") then
      local ok, t = pcall(ResolveWideStringBinding, "Autolog.Adapter.CurrentItem.FeedText")
      if ok and type(t) == "string" then second = t end
    end
  end
  return { second, s.E_NAVIGATION_GO_BACK, s.E_NAVIGATION_PLAY }
end

local function updateNav()
  if SetStringVariable == nil or util == nil or util.ResolveWideStringTable == nil then return end
  local s = {}
  if not pcall(util.ResolveWideStringTable, "Autolog.Feedback.Navigation", kNav, s) then return end
  local out = kRaceSettingsPrompt
  for _, p in ipairs(gameNavRight(s)) do
    if type(p) == "string" and #p > 0 then out = out .. kSep .. p end
  end
  pcall(SetStringVariable, "NavRight", out)
end

-- info box text (mode, difficulty, opponents, weapons): a multi-line string from the helper
local function updateSummary()
  if ResolveStringBinding == nil or SetStringVariable == nil then return end
  local ok, sum = pcall(ResolveStringBinding, "SR.Summary")
  if ok and type(sum) == "string" then pcall(SetStringVariable, "Summary", sum) end
end

-- location map: "12 TRACKS" under the location name (the location's event list; on the event list CurrentItem is an
-- event, so the last location's text is kept)
local function updateTracks()
  if ResolveIntBinding == nil or SetStringVariable == nil then return end
  local ok, n = pcall(ResolveIntBinding, "Autolog.Adapter.CurrentItem.NumEvents")
  n = ok and tonumber(n) or nil
  if n ~= nil and n > 0 then
    pcall(SetStringVariable, "Tracks", n == 1 and "1 TRACK" or (tostring(n) .. " TRACKS"))
  end
end

local function update()
  updateNav()
  updateSummary()
  updateTracks()
end

function sr_info.Construct()
  if ReserveInputMappings ~= nil and AddMappedInput ~= nil then
    pcall(ReserveInputMappings, 1)
    pcall(AddMappedInput, "to_main_menu", E_RACE_SETTINGS, false, false)
  end
  update()
  return true
end
function sr_info.SetConfigurationItem(a, b) return true end
function sr_info.OnPageLoaded() update() return true end
function sr_info.OnPageUnloading() return true end
function sr_info.PreInputUpdate(dt) update() return true end
function sr_info.HandleInputPressed(input)
  if input == E_RACE_SETTINGS then
    pcall(ResolveStringBinding, "SR.OpenSettings")
    return true
  end
  return false
end
