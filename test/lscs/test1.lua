lastin = ""

function processing()
    lastin = getinput()
    if lastin == "" then return end
    print(lastin)
    printout("You've entered '" .. lastin .. "'")
end

function outpin(pin)
    print(pin)
    printout("Sending data from " .. getname() .. " pin " .. pin .. ": " .. lastin)
    return lastin
end

local s = "Hello from Aria pod ver." .. getversion() .. " named " .. getname()
print(s)
printout(s)
setiocount(0,1)
