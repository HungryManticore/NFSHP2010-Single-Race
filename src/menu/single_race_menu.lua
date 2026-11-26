-- Single Race mod: the main menu's Wall item opens the multiplayer event picker offline (its copy in WALL.BNDL).
-- Loaded after main_menu.lua (listed last in MAIN_MENU.JSON "scripts"). It only wraps the game's own
-- functions; when the selected item is not Wall, everything runs exactly as before.

single_race_menu = {}

local function log(s)
  if print ~= nil then print("[SR] " .. tostring(s)) end
end

-- Remember the input id the main menu registers for "accept" (its own enum is a local we cannot read).
if SR_OrigAddMappedInput == nil and AddMappedInput ~= nil then
  SR_OrigAddMappedInput = AddMappedInput
  AddMappedInput = function(name, id, a, b)
    if SR_CaptureFor ~= nil and name == "accept" then
      SR_CaptureFor.SR_AcceptId = id
    end
    return SR_OrigAddMappedInput(name, id, a, b)
  end
end

-- While the game's own accept handler runs (SR_WallOffline), the Wall item reports no connection needs
-- and its transition art is the picker's.
local function wall_selected()
  local ok, link = pcall(ResolveStringBinding, "Autolog.Adapter.Selected.Link")
  return ok and link == "wall"
end
if SR_OrigResolveBool == nil and ResolveBoolBinding ~= nil then
  SR_OrigResolveBool = ResolveBoolBinding
  ResolveBoolBinding = function(name, ...)
    if SR_WallOffline and (name == "Autolog.Adapter.Selected.NeedsConnection" or name == "Autolog.Adapter.Selected.WantsConnection"
        or name == "Autolog.Adapter.Selected.NeedsOnlinePass") and wall_selected() then
      return false
    end
    return SR_OrigResolveBool(name, ...)
  end
end
if SR_OrigSetTransition == nil and SetTransitionDestination ~= nil then
  SR_OrigSetTransition = SetTransitionDestination
  SetTransitionDestination = function(dest, ...)
    if SR_WallOffline and dest == "wall" then
      log("Wall -> Single Race picker (through the menu's offline route)")
      WriteBoolToOnlineSession("SingleRace", true)
      dest = "event_select"
    end
    return SR_OrigSetTransition(dest, ...)
  end
end

local function open_single_race()
  log("Wall -> Single Race picker")
  WriteBoolToOnlineSession("SingleRace", true)
  -- transition art of the picker (the Wall art flashed before the picker)
  if SetTransitionDestination ~= nil then SetTransitionDestination("event_select") end
  OverrideDefaultPageForContext("offline_menu", "wall")
  ExitContextViaHsmPoint("TO_OFFLINE")
end

local function wrap(mm)
  if mm == nil or mm.SR_Wrapped then return end
  mm.SR_Wrapped = true

  local origConstruct = mm.Construct
  mm.Construct = function(...)
    SR_CaptureFor = mm
    local r = origConstruct(...)
    SR_CaptureFor = nil
    log("main menu accept id = " .. tostring(mm.SR_AcceptId))
    return r
  end

  -- a dialog (the quit confirmation, a user_feedback popup) open now or within the last half second blocks
  -- the redirect: its Accept may close it before this handler runs. Not the binding
  -- Autolog.Controls.Main_menu.DialogActive: offline it also stays true for the live feedback messages
  -- ("connect to Autolog"), which blocked the redirect and let the menu show its "get online" popup.
  local function dialog_open()
    if quitgame ~= nil and quitgame.ShowDialogActive == true then return true end
    if user_feedback ~= nil and (user_feedback.mbShowDialog == true or user_feedback.mbDialogJustRequested == true) then
      return true
    end
    return false
  end
  mm.SR_DialogCooldown = 0
  local origPre = mm.PreInputUpdate
  mm.PreInputUpdate = function(dt, ...)
    if dialog_open() then mm.SR_DialogCooldown = 0.5
    elseif type(dt) == "number" and mm.SR_DialogCooldown > 0 then mm.SR_DialogCooldown = mm.SR_DialogCooldown - dt end
    return origPre(dt, ...)
  end

  local origInput = mm.HandleInputPressed
  mm.HandleInputPressed = function(input, ...)
    local dialog = dialog_open() or mm.SR_DialogCooldown > 0
    if mm.SR_AcceptId ~= nil and input == mm.SR_AcceptId then
      local link = ResolveStringBinding("Autolog.Adapter.Selected.Link")
      log("accept on '" .. tostring(link) .. "'" .. (dialog and " (dialog open - left to the menu)" or ""))
      if link == "wall" and not dialog then
        local ok, err = pcall(open_single_race)
        if ok then return true end
        log("open failed: " .. tostring(err))
      end
    end
    -- whatever reaches the game's handler: Wall (Single Race) never needs a connection, so it takes the
    -- offline route there too instead of the "get online" popup
    SR_WallOffline = true
    local r = origInput(input, ...)
    SR_WallOffline = false
    return r
  end
  log("main menu wrapped")
end

wrap(main_menu)
