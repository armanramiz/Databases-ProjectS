drop table if exists Items
drop table if exists Users
drop table if exists Bids
drop table if exists Item_Categories

create table Items(
    ItemID INTEGER PRIMARY KEY, 
    Seller TEXT FOREIGN KEY, 
    Name TEXT NOT NULL, 
    Description TEXT NOT NULL, 
    Currently DECIMAL NOT NULL,
    Number_of_Bids INTEGER NOT NULL,
    Location TEXT,
    Country TEXT,
    Started TIMESTAMP NOT NULL,
    Ends TIMESTAMP NOT NULL);
          
create table Users( 
    UserID TEXT PRIMARY KEY,
    Location TEXT,
    Rating REAL NOT NULL,
    Country TEXT);

create table Bids(
    FOREIGN KEY (ItemID) REFERENCES Items(ItemID),
    FOREIGN KEY (UserID) REFERENCES Users(UserID),
    Time TIMESTAMP NOT NULL,
    Amount DECIMAL NOT NULL   
);

create table Item_Categories (
    FOREIGN KEY (ItemID) REFERENCES Items(ItemID),
    CategoryName TEXT NOT NULL,
    PRIMARY KEY (ItemID, CategoryName)
);