drop table if exists Items;
drop table if exists Users;
drop table if exists Bids;
drop table if exists Item_Categories;
create table Users (
    UserID TEXT PRIMARY KEY NOT NULL,
    Rating REAL NOT NULL,
    Location TEXT,
    Country TEXT);
create table Items(
    ItemID INTEGER PRIMARY KEY, 
    SellerID TEXT, 
    Name TEXT NOT NULL, 
    Description TEXT, 
    Currently DECIMAL NOT NULL,
    Buy_Price DECIMAL,
    First_bid DECIMAL,
    Number_of_Bids INTEGER NOT NULL,
    Location TEXT NOT NULL,
    Country TEXT NOT NULL,
    Started TIMESTAMP NOT NULL,
    Ends TIMESTAMP NOT NULL,
    FOREIGN KEY (SellerID) REFERENCES Users(UserID)
);     
create table Bids(
    ItemID INTEGER, 
    BidderID TEXT,
    Time TIMESTAMP NOT NULL,
    Amount DECIMAL NOT NULL,
    PRIMARY KEY (ItemID, bidderID, Time),
    FOREIGN KEY (ItemID) REFERENCES Items(ItemID),
    FOREIGN KEY (BidderID) REFERENCES Users(UserID)
);
create table Item_Categories (
    ItemID INTEGER,
    CategoryName TEXT NOT NULL,
    FOREIGN KEY (ItemID) REFERENCES Items(ItemID),
    PRIMARY KEY (ItemID, CategoryName)
);