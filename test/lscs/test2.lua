lastin = ""

function processing()
    if lastin == "" then return end
    print(lastin)
    printout("Slave received '" .. lastin .. "'")
    --if brainstate() == "not loaded" then
        brainstart("llava-7b-q5_k.gguf","")
        brainsetvenc("mmproj-llava-7b-f16.gguf")
        brainin("SYSTEM: You're a helpful AI assistant named Anna. You're helping your user with their daily tasks.\n")
        printout("Slave: image attached: '" .. lastin .. "'")
        if not brainloadimage(lastin) then
            printout("I can't open image file " .. lastin .. ": " .. brainerror())
            brainstop()
            return
        end
        brainin("\nUSER: Please describe the image.\n")
        brainprefix("Anna:")
        while true do
            while not brainprocess(false) do
                print("processing....")
            end
            if brainstate() ~= "ready" then break end
            print("generating...")
        end
        printout("Slave: " .. brainout())
        brainstop()
    --end
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
