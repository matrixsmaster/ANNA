lastin = ""

function processing()
    if lastin == "" then return end
    print(lastin)
    printout("We've received '" .. lastin .. "'")
    lastin = ""
end

function inpin(pin,msg)
    print(pin,msg)
    printout("Message '" .. msg .. "' has been received at " .. getname() .. " for pin " .. pin)
    lastin = msg
    return true
end

local s = "Hello from another Aria pod ver." .. getversion() .. " named " .. getname()
print(s)
printout(s)
setiocount(1,0)
