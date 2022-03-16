import sqlite3
con = sqlite3.connect('ebay.db')
cur = con.cursor()

for row in cur.execute('SELECT * FROM users'):
    print(row)

# We can also close the connection if we are done with it.
# Just be sure any changes have been committed or they will be lost.
con.close()