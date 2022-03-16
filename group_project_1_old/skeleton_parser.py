import sys, os
from json import loads
from re import sub

columnSeparator="|"
EMPTY_SPOT=""

# Dictionary of months used for date transformation
MONTHS = {'Jan':'01','Feb':'02','Mar':'03','Apr':'04','May':'05','Jun':'06',\
        'Jul':'07','Aug':'08','Sep':'09','Oct':'10','Nov':'11','Dec':'12'}

"""
Returns true if a file ends in .json
"""
def isJson(f):
    return len(f) > 5 and f[-5:] == '.json'

"""
Converts month to a number, e.g. 'Dec' to '12'
"""
def transformMonth(mon):
    if mon in MONTHS:
        return MONTHS[mon]
    else:
        return mon

"""
Transforms a timestamp from Mon-DD-YY HH:MM:SS to YYYY-MM-DD HH:MM:SS
"""
def transformDttm(dttm):
    dttm = dttm.strip().split(' ')
    dt = dttm[0].split('-')
    date = '20' + dt[2] + '-'
    date += transformMonth(dt[0]) + '-' + dt[1]
    return date + ' ' + dttm[1]

"""
Transform a dollar value amount from a string like $3,453.23 to XXXXX.xx
"""

def transformDollar(money):
    if money == None or len(money) == 0:
        return money
    return sub(r'[^\d.]', '', money)

"""
Parses a single json file. Currently, there's a loop that iterates over each
item in the data set. Your job is to extend this functionality to create all
of the necessary SQL tables for your database.
"""
def parseJson(json_file):
    try:
        with open("users.dat", "r") as users_table:
            existing_user_data = {user.split(columnSeparator)[0] : user.split(columnSeparator)[1:] for user in users_table.read().split('\n')}
    except FileNotFoundError:
        existing_user_data = {}
    with open(json_file, 'r') as f, \
    open("items.dat", "a") as items_table,  \
    open("bids.dat", "a") as bids_table, \
    open("item_categories.dat", "a") as item_categories_table:
        items = loads(f.read())['Items'] # creates a Python dictionary of Items for the supplied json file
        
        for item in items:
            item_line=item["ItemID"]+columnSeparator
            item_line+=item["Seller"]["UserID"]+columnSeparator
            item_line+=item["Name"]+columnSeparator
            if(item["Description"] is not None):
                item_line+=item["Description"]+columnSeparator
            else:
                item_line+=EMPTY_SPOT+columnSeparator
            item_line+=transformDollar(item["Currently"])+columnSeparator
            try:
                item_line+=transformDollar(item["Buy_Price"])+columnSeparator
            except KeyError:
                item_line+=EMPTY_SPOT+columnSeparator
            item_line+=transformDollar(item["First_Bid"])+columnSeparator
            item_line+=item["Number_of_Bids"]+columnSeparator
            item_line+=item["Location"]+columnSeparator
            item_line+=item["Country"]+columnSeparator
            item_line+=transformDttm(item["Started"])+columnSeparator
            item_line+=transformDttm(item["Ends"])
            for category in item["Category"]:
                item_categories_table.write(item["ItemID"]+columnSeparator+category+"\n")
            
            
            
            if(item["Bids"] is not None):
                for bid in item["Bids"]:
                    bid=bid["Bid"]
                    bidder=bid["Bidder"]
                    bids_table.write(item["ItemID"]+columnSeparator+bidder["UserID"]+columnSeparator+transformDttm(bid["Time"])+columnSeparator+transformDollar(bid["Amount"])+"\n")
                    
                    existing_user_data[bidder["UserID"]] = [bidder["Rating"], bidder.get("Location",EMPTY_SPOT), bidder.get("Country",EMPTY_SPOT)]
                            
            if item["Seller"]["UserID"] not in existing_user_data.keys():
                existing_user_data[item["Seller"]["UserID"]] = [item["Seller"]["Rating"], EMPTY_SPOT, EMPTY_SPOT]
            items_table.write(item_line+"\n")
            pass
    with open("users.dat", "w") as users_table:
        users_table.write('\n'.join([columnSeparator.join([user]+row) for user, row in existing_user_data.items()]))


"""
Loops through each json files provided on the command line and passes each file
to the parser
"""
def main(argv):
    if os.path.exists("bids.dat"):
        os.remove("bids.dat")
    if os.path.exists("item_categories.dat"):
        os.remove("item_categories.dat")
    if os.path.exists("items.dat"):
        os.remove("items.dat")
    if os.path.exists("users.dat"):
        os.remove("users.dat")
    if len(argv) < 2:
        #print >> sys.stderr, 'Usage: python skeleton_json_parser.py <path to json files>'
        sys.exit(1)
    # loops over all .json files in the argument
    for f in argv[1:]:
        if isJson(f):
            parseJson(f)
            print("Success parsing " + str(f))
#parseJson("test.json")
if __name__ == '__main__':
    main(sys.argv)
 
