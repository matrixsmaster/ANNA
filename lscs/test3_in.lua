outa = ""
outb = ""

function processing()
    print(getname().. " processing...")
    outa = getinput()
    print(outa)
    outb = "processing() called in ".. getname()
end

function outpin(pin)
    local t
    if pin == 0 then
        t = outa
        outa = ""
    else
        t = outb
        outb = ""
    end
    return t
end

setiocount(0,2)