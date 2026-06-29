-- Examples aligned with Neon SQL/PGQ documentation (fixed-depth patterns).
SET yb_enable_property_graph_queries = on;

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    email TEXT,
    joined_at DATE DEFAULT CURRENT_DATE
);

CREATE TABLE follows (
    id SERIAL PRIMARY KEY,
    follower_id INT NOT NULL REFERENCES users(id),
    followed_id INT NOT NULL REFERENCES users(id),
    created_at TIMESTAMP DEFAULT now()
);

CREATE TABLE posts (
    id SERIAL PRIMARY KEY,
    author_id INT NOT NULL REFERENCES users(id),
    title TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT now()
);

CREATE TABLE likes (
    id SERIAL PRIMARY KEY,
    user_id INT NOT NULL REFERENCES users(id),
    post_id INT NOT NULL REFERENCES posts(id),
    created_at TIMESTAMP DEFAULT now()
);

INSERT INTO users (name, email) VALUES
    ('Alice', 'alice@example.com'),
    ('Bob', 'bob@example.com'),
    ('Charlie', 'charlie@example.com'),
    ('Diana', 'diana@example.com');

INSERT INTO follows (follower_id, followed_id) VALUES
    (1, 2), (1, 3), (2, 3), (3, 4), (4, 1);

INSERT INTO posts (author_id, title) VALUES
    (1, 'Getting Started with PostgreSQL'),
    (2, 'Graph Queries in SQL'),
    (3, 'Why I Switched from Neo4j');

INSERT INTO likes (user_id, post_id) VALUES
    (2, 1), (3, 1), (4, 2), (1, 3), (2, 3);

CREATE PROPERTY GRAPH social_graph
  VERTEX TABLES (
    users LABEL person
      PROPERTIES (id, name, email, joined_at),
    posts LABEL post
      PROPERTIES (id, title, created_at)
  )
  EDGE TABLES (
    follows
      SOURCE KEY (follower_id) REFERENCES users (id)
      DESTINATION KEY (followed_id) REFERENCES users (id)
      LABEL follows
      PROPERTIES (created_at),
    likes
      SOURCE KEY (user_id) REFERENCES users (id)
      DESTINATION KEY (post_id) REFERENCES posts (id)
      LABEL liked
      PROPERTIES (created_at)
  );

-- Find who Alice follows
SELECT * FROM GRAPH_TABLE (social_graph
    MATCH (a IS person WHERE a.name = 'Alice')
          -[f IS follows]->(b IS person)
    COLUMNS (b.name AS followed_name)
) ORDER BY followed_name;

-- Friends of friends
SELECT * FROM GRAPH_TABLE (social_graph
    MATCH (a IS person WHERE a.name = 'Alice')
          -[IS follows]->(b IS person)
          -[IS follows]->(c IS person)
    COLUMNS (
        b.name AS friend,
        c.name AS friend_of_friend
    )
) ORDER BY friend, friend_of_friend;

-- Combine with SQL aggregation
SELECT followed_name, count(*) AS follower_count
FROM GRAPH_TABLE (social_graph
    MATCH (a IS person)-[IS follows]->(b IS person)
    COLUMNS (b.name AS followed_name)
)
GROUP BY followed_name
HAVING count(*) > 1
ORDER BY follower_count DESC, followed_name;

-- Feature disabled rejects DDL
SET yb_enable_property_graph_queries = off;
CREATE PROPERTY GRAPH should_fail
  VERTEX TABLES (users LABEL person);
SET yb_enable_property_graph_queries = on;

DROP PROPERTY GRAPH social_graph;
DROP TABLE likes, posts, follows, users;
