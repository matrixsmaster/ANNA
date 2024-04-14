lastin = ""
output = ""

function processing()
    lastin = getinput()
    output = ""
    if lastin ~= "" then
        printout("You've entered '" .. lastin .. "'")
        brainin(lastin)
        brainprefix("Anna:")
    end
    while not brainprocess(false) do
        print("processing....")
    end
    output = brainout()
end

function outpin(pin)
    print(pin)
    printout("Sending data from " .. getname() .. " pin " .. pin .. ": " .. output)
    return output
end

local s = "Hello from main Aria pod ver." .. getversion() .. " named " .. getname()
print(s)
printout(s)
setiocount(0,1)

if (brainstart("zephyr-7b-beta.Q3_K_M.gguf","")) then
    printout("Brain started")
else
    printout("Can't start the brain")
end
